#!/usr/bin/env python3
"""GSGate: a web gateway for Apple IIgs browsers (step 1: text only).

It works as an ordinary HTTP proxy. The IIgs browser asks it for a page; it fetches the page (HTTP or
HTTPS), throws away scripts, styles and everything else an old browser can't use, and answers with a
small, strict HTML page. Files that are not pages are passed through unchanged.

    python3 gsgate.py                 listen on port 8080, serve this computer only (emulators)
    python3 gsgate.py --lan           also serve other machines in the home network (a real IIgs)
    python3 gsgate.py --port 3128

An emulator that starts the gateway itself uses:
    --bind 127.0.0.1        listen on this computer only, invisible to the network
    --alias 192.168.64.254  the address the emulated IIgs has for this computer; pages asked from
                            that address are fetched from this computer's own web server
    --watch-stdin           stop when the emulator goes away (its end of the pipe closes)
    --log FILE              write the log to a file

Only the Python standard library is used. See ../docs/BROWSER_PLAN.md."""
import argparse
import html
import ipaddress
import re
import socket
import os
import sys
import tempfile
import threading
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from html.parser import HTMLParser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gspic  # noqa: E402

PIC_HOST = "gsgate.invalid"     # pictures are asked for at this made-up host; only the gateway answers it

VERSION = "0.4"
FORMAT = "1"                    # version of the mini page format (the contract with the browser)
MAX_FETCH = 3 * 1024 * 1024     # largest page we read
MAX_PASS = 40 * 1024 * 1024     # largest download we pass through
MAX_PAGE = 48 * 1024            # largest page we send to the IIgs
TIMEOUT = 20
ALIAS = None                    # the emulated IIgs's address for this computer (--alias)
AGENT = "Mozilla/5.0 (compatible; GSGate/%s; Apple IIgs gateway)" % VERSION

# ---- text ----

_PUNCT = {
    0x2018: "'", 0x2019: "'", 0x201A: "'", 0x201B: "'", 0x201C: '"', 0x201D: '"', 0x201E: '"',
    0x2013: "-", 0x2014: "-", 0x2212: "-", 0x2010: "-", 0x2011: "-", 0x2026: "...", 0x00A0: " ",
    0x2022: "*", 0x00B7: "*", 0x00AB: '"', 0x00BB: '"', 0x2039: "'", 0x203A: "'", 0x00DF: "ss",
    0x00C6: "AE", 0x00E6: "ae", 0x00D8: "O", 0x00F8: "o", 0x0152: "OE", 0x0153: "oe", 0x20AC: "EUR",
    0x00A3: "GBP", 0x00A9: "(c)", 0x00AE: "(R)", 0x2122: "(TM)", 0x00B0: " deg", 0x00D7: "x",
    0x2190: "<-", 0x2192: "->", 0x2191: "^", 0x2193: "v", 0x00C4: "Ae", 0x00D6: "Oe", 0x00DC: "Ue",
    0x00E4: "ae", 0x00F6: "oe", 0x00FC: "ue", 0x200B: "", 0x200C: "", 0x200D: "", 0xFEFF: "", 0x00AD: "",
}


def plain(text):
    """Text in plain ASCII: accents removed, typographic characters replaced."""
    out = []
    for ch in text:
        c = ord(ch)
        if c < 127:
            out.append(ch)
        elif c in _PUNCT:
            out.append(_PUNCT[c])
        else:
            d = unicodedata.normalize("NFKD", ch).encode("ascii", "ignore").decode("ascii")
            out.append(d)           # unknown characters (other scripts, emoji) disappear
    return "".join(out)


def esc(text):
    return plain(text).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


# ---- the page converter ----

SKIP = {"script", "style", "noscript", "svg", "canvas", "iframe", "object", "embed", "video", "audio",
        "template", "form", "button", "select", "textarea", "dialog", "map", "picture", "source", "head"}
CHROME = {"nav", "header", "footer", "aside"}       # page furniture: kept out of the text, links collected
BLOCK = {"p", "div", "section", "article", "main", "figure", "figcaption", "address", "details",
         "summary", "dl", "dt", "dd", "fieldset", "table", "tbody", "thead", "tfoot", "caption"}
VOID = {"br", "hr", "img", "input", "meta", "link", "area", "base", "col", "wbr", "source", "track", "embed"}
INLINE = {"b": "B", "strong": "B", "i": "I", "em": "I", "cite": "I", "u": "U", "ins": "U"}


class Converter(HTMLParser):
    def __init__(self, base, main_only, pics=None, pic_width=600):
        super().__init__(convert_charrefs=True)
        self.base = base
        self.pics = pics            # None: no pictures (Webber); "640": pictures for the page window
        self.pic_width = pic_width
        self.main_only = main_only  # the page has <main>/<article>: keep only what is inside
        self.out = []               # finished pieces of the page
        self.line = []              # the paragraph being collected
        self.title = ""
        self.stack = []             # open source tags
        self.skip = 0               # inside a tag whose content is dropped
        self.chrome = 0             # inside nav/header/footer
        self.inmain = 0
        self.pre = 0
        self.in_title = False
        self.lists = []             # open UL/OL
        self.href = None            # the open link, if any
        self.linktext = []
        self.styles = []            # open B/I/U
        self.site_links = []        # links from the page furniture
        self.cell_open = False
        self.size = 0
        self.cut = False

    # -- helpers --
    def visible(self):
        return not self.skip and not self.chrome and (self.inmain or not self.main_only)

    def emit(self, s):
        if self.cut:
            return
        self.size += len(s)
        if self.size > MAX_PAGE:
            self.cut = True
            return
        self.out.append(s)

    def flush(self, tag="P"):
        text = re.sub(r"[ \t\r\n]+", " ", "".join(self.line)).strip()
        self.line = []
        if "<IMG" not in text and not re.sub(r"<[^>]+>", "", text).strip(" |*-"):
            return                  # nothing, or only the separators of empty table cells
        if tag:
            self.emit("<%s>%s</%s>\n" % (tag, text, tag))
        else:
            self.emit(text + "\n")

    def link(self, href):
        if not href:
            return None
        href = href.strip()
        low = href.lower()
        if low.startswith(("javascript:", "mailto:", "tel:", "data:", "#")):
            return None
        url = urllib.parse.urljoin(self.base, href)
        url = urllib.parse.urldefrag(url)[0]
        if url.lower().startswith("https://"):      # old browsers refuse https links; we fetch them anyway
            url = "http://" + url[8:]
        if not url.lower().startswith("http://"):
            return None
        return plain(url).replace('"', "%22").replace(" ", "%20").replace("<", "%3C").replace(">", "%3E")

    # -- parser callbacks --
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        kind = None
        if tag == "title" and not self.title:
            self.in_title = True
        elif tag in SKIP:
            kind = "skip"
        elif "hidden" in a or a.get("aria-hidden") == "true":
            kind = "skip"
        elif tag in CHROME or a.get("role") in ("navigation", "banner", "contentinfo", "complementary"):
            kind = "chrome"
        elif tag in ("main", "article") or a.get("role") == "main":
            kind = "main"
        if tag in VOID:
            if kind is None and not self.skip:
                self.void(tag, a)
            return
        self.stack.append((tag, kind))
        if kind == "skip":
            self.skip += 1
        elif kind == "chrome":
            self.chrome += 1
        elif kind == "main":
            self.inmain += 1
        if kind in ("skip", "chrome") or self.skip:
            return
        if tag == "a":
            url = self.link(a.get("href"))
            if url and self.href is None:
                if self.chrome:
                    self.href, self.linktext = url, []
                elif self.visible():
                    self.href, self.linktext = url, []
                    self.line.append('<A HREF="%s">' % url)
            return
        if not self.visible():
            return
        if tag in ("h1", "h2", "h3", "h4", "h5", "h6"):
            self.flush()
        elif tag in INLINE:
            t = INLINE[tag]
            if t not in self.styles:
                self.styles.append(t)
                self.line.append("<%s>" % t)
        elif tag in ("ul", "ol", "menu"):
            self.flush("LI" if self.lists else "P")
            t = "OL" if tag == "ol" else "UL"
            self.lists.append(t)
            self.emit("<%s>\n" % t)
        elif tag == "li":
            self.flush("LI" if self.lists else "P")
        elif tag == "pre":
            self.flush()
            self.pre += 1
        elif tag == "blockquote":
            self.flush()
            self.emit("<BLOCKQUOTE>\n")
        elif tag == "tr":
            self.flush()
            self.cell_open = False
        elif tag in ("td", "th"):
            if self.cell_open:
                self.line.append(" | ")
            self.cell_open = True
        elif tag in BLOCK:
            self.flush()

    def void(self, tag, a):
        if not self.visible():
            return
        if tag == "br":
            self.line.append("\n" if self.pre else "<BR>")
        elif tag == "hr":
            self.flush()
            self.emit("<HR>\n")
        elif tag == "img":
            alt = (a.get("alt") or "").strip()
            tagtext = self.picture(a, alt) if self.pics else None
            if tagtext:
                self.line.append(tagtext)
            elif alt:
                self.line.append("[%s]" % esc(alt))

    def picture(self, a, alt):
        """An IMG tag that asks the gateway for the converted picture, or None to leave the picture out."""
        src = a.get("src") or a.get("data-src") or ""
        if not src or src.lower().startswith("data:") or ".svg" in src.lower().split("?")[0]:
            return None
        url = urllib.parse.urljoin(self.base, src.strip())
        if not url.lower().startswith(("http://", "https://")):
            return None
        size = ""
        try:
            w, h = int(a.get("width", "")), int(a.get("height", ""))
            if w < 24 or h < 24:
                return None             # tracking pixels, bullets and spacers
            tw, th = gspic.target_size(w, h, self.pics, self.pic_width)
            # WIDTH and HEIGHT are in square "web" pixels; the browser turns them into screen lines
            size = ' WIDTH="%d" HEIGHT="%d"' % (tw, round(th / gspic.ASPECT[self.pics]))
        except ValueError:
            pass
        q = urllib.parse.urlencode({"k": self.pics, "w": self.pic_width, "u": url})
        return '<IMG SRC="http://%s/pic?%s"%s ALT="%s">' % (PIC_HOST, q, size, esc(alt).replace('"', "'"))

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        if tag == "title":
            self.in_title = False
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i][0] == tag:
                break
        else:
            return                  # an end tag without a start tag
        while len(self.stack) > i:  # sloppy pages leave tags open: close them too
            self.close_one(*self.stack.pop())

    def close_one(self, top, kind):
        if kind == "skip":
            self.skip -= 1
            return
        if kind == "chrome":
            self.chrome -= 1
            if top == "a":
                self.href = None
            return
        if self.skip:
            return
        if top == "a":
            if self.href is not None:
                text = re.sub(r"\s+", " ", "".join(self.linktext)).strip()
                if self.chrome:
                    if text and len(self.site_links) < 30 and (self.href, text) not in self.site_links:
                        self.site_links.append((self.href, text))
                elif self.visible():
                    self.line.append("</A>")
                self.href = None
            return
        if self.visible():
            if top in ("h1", "h2", "h3"):
                self.flush(top.upper())
            elif top in ("h4", "h5", "h6"):
                self.line = ["<B>", "".join(self.line), "</B>"]
                self.flush()
            elif top in INLINE:
                t = INLINE[top]
                if t in self.styles:
                    self.styles.remove(t)
                    self.line.append("</%s>" % t)
            elif top in ("ul", "ol", "menu"):
                self.flush("LI" if self.lists else "P")
                if self.lists:
                    self.emit("</%s>\n" % self.lists.pop())
            elif top == "li":
                self.flush("LI" if self.lists else "P")
            elif top == "pre":
                text = "".join(self.line).strip("\n")
                self.line = []
                self.pre = max(0, self.pre - 1)
                if text.strip():
                    self.emit("<PRE>%s</PRE>\n" % text)
            elif top == "blockquote":
                self.flush()
                self.emit("</BLOCKQUOTE>\n")
            elif top == "tr":
                self.flush()
                self.cell_open = False
            elif top in BLOCK or kind == "main":
                self.flush()
        if kind == "main":
            self.inmain -= 1

    def handle_data(self, data):
        if self.in_title:
            self.title += data
            return
        if self.skip:
            return
        if self.href is not None:
            self.linktext.append(esc(data))
        if not self.visible():
            return
        self.line.append(esc(data))

    def result(self, url):
        self.flush()
        while self.lists:
            self.emit("</%s>\n" % self.lists.pop())
        title = re.sub(r"\s+", " ", plain(self.title)).strip() or urllib.parse.urlsplit(url).netloc
        body = "".join(self.out)
        parts = ["<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n<BODY>\n" % esc(title)]
        if "<H1>" not in body[:600]:
            parts.append("<H1>%s</H1>\n" % esc(title))
        parts.append(body if body.strip() else "<P>This page has no text that can be shown without scripts.</P>\n")
        if self.cut:
            parts.append("<HR>\n<P><I>The page was cut here because it is very long.</I></P>\n")
        if self.site_links:
            parts.append("<HR>\n<P><B>Site links</B></P>\n<UL>\n")
            for href, text in self.site_links:
                parts.append('<LI><A HREF="%s">%s</A></LI>\n' % (href, text))
            parts.append("</UL>\n")
        parts.append("</BODY>\n</HTML>\n")
        return "".join(parts)


def convert(text, url, pics=None, pic_width=600):
    """A web page as mini HTML (format 1)."""
    low = text.lower()
    main_only = bool(re.search(r"<(main|article)[\s>]|role=[\"']main[\"']", low))
    for attempt in (main_only, False):
        c = Converter(url, attempt, pics, pic_width)
        try:
            c.feed(text)
            c.close()
        except Exception:           # a broken page must not stop the gateway
            pass
        page = c.result(url)
        # if "main only" left almost nothing, the page marks its content differently: take everything
        if not attempt or len(re.sub(r"<[^>]+>", "", "".join(c.out))) > 200:
            return page
    return page


# ---- fetching ----

def own_addresses():
    out = {"127.0.0.1"}
    try:
        out.update(i[4][0] for i in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET))
    except OSError:
        pass
    try:                            # the address used for the default route
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.0.2.1", 9))
        out.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    return out


def target_allowed(host, allow_lan):
    """The gateway is for the public web: no requests to this computer or the home network,
    except this computer's own web server (the shop under test)."""
    if ALIAS and host == ALIAS:
        return True
    try:
        infos = socket.getaddrinfo(host, None)
    except OSError:
        return True                 # let the fetch report the name error
    mine = own_addresses()
    for info in infos:
        ip = ipaddress.ip_address(info[4][0])
        if str(ip) in mine and not ip.is_loopback:
            continue
        if ip.is_loopback or ip.is_link_local or ip.is_multicast or ip.is_unspecified:
            return False
        if ip.is_private and not allow_lan:
            return False
    return True


def decode(raw, ctype):
    m = re.search(r"charset=([\w-]+)", ctype or "", re.I)
    enc = m.group(1) if m else None
    if not enc:
        m = re.search(rb"<meta[^>]+charset=[\"']?([\w-]+)", raw[:4096], re.I)
        enc = m.group(1).decode("ascii", "replace") if m else "utf-8"
    try:
        return raw.decode(enc, "replace")
    except LookupError:
        return raw.decode("utf-8", "replace")


def fetch(url):
    """(final url, content type, body bytes, is_page). http is tried first; redirects to https are followed."""
    last = None
    tries = [url]
    parts = urllib.parse.urlsplit(url)
    local = bool(ALIAS) and parts.hostname == ALIAS
    if local:                       # this computer, as the emulated IIgs calls it
        tries = [urllib.parse.urlunsplit(parts._replace(netloc="127.0.0.1" + (":%d" % parts.port if parts.port else "")))]
    elif url.lower().startswith("http://"):
        tries.append("https://" + url[7:])
    for u in tries:
        req = urllib.request.Request(u, headers={"User-Agent": AGENT, "Accept": "text/html,*/*;q=0.5",
                                                 "Accept-Language": "en,de;q=0.7"})
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                ctype = r.headers.get("Content-Type", "application/octet-stream")
                is_page = ctype.split(";")[0].strip().lower() in ("text/html", "application/xhtml+xml")
                body = r.read((MAX_FETCH if is_page else MAX_PASS) + 1)
                final = r.geturl()
                if local:           # links in the page must use the IIgs's name for this computer again
                    fp = urllib.parse.urlsplit(final)
                    if fp.hostname == "127.0.0.1":
                        final = urllib.parse.urlunsplit(fp._replace(netloc=ALIAS + (":%d" % fp.port if fp.port else "")))
                return final, ctype, body, is_page
        except urllib.error.HTTPError as e:
            raise
        except (urllib.error.URLError, OSError, ValueError) as e:
            last = e
    raise last


_cache = {}                     # url -> (time, result); browsers ask with HEAD and then with GET


def cached_fetch(url):
    now = time.time()
    for k in [k for k, v in _cache.items() if now - v[0] > 120]:
        _cache.pop(k, None)
    hit = _cache.get(url)
    if hit:
        return hit[1]
    result = fetch(url)
    if len(result[2]) <= MAX_FETCH:
        _cache[url] = (now, result)
    return result


_pics = {}                      # (kind, width, url) -> converted picture


def picture(kind, width, url):
    """The picture at url as an Apple Preferred Format file for the IIgs."""
    key = (kind, width, url)
    if key in _pics:
        return _pics[key]
    _final, _ctype, body, _is_page = cached_fetch(url)
    with tempfile.TemporaryDirectory() as tmp:
        ext = os.path.splitext(urllib.parse.urlsplit(url).path)[1].lower()
        path = os.path.join(tmp, "in" + (ext if 1 < len(ext) <= 5 else ".img"))
        with open(path, "wb") as f:
            f.write(body)
        data, _w, _h = gspic.convert_file(path, kind, width)
    if len(_pics) > 200:
        _pics.clear()
    _pics[key] = data
    return data


def message(title, text):
    return ("<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n<BODY>\n<H1>%s</H1>\n<P>%s</P>\n<HR>\n"
            "<P><I>GSGate %s</I></P>\n</BODY>\n</HTML>\n" % (esc(title), esc(title), esc(text), VERSION))


# ---- the proxy server ----

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "GSGate/" + VERSION

    def send_page(self, code, page):
        data = page.encode("ascii", "replace")
        self.send_response(code)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-GSGate-Format", FORMAT)
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def client_wants(self):
        """(picture kind or None, window width) from the header a picture-capable browser sends:
        X-GSGate-Client: pictures=640; width=600"""
        opts = dict(p.strip().split("=", 1) for p in self.headers.get("X-GSGate-Client", "").split(";") if "=" in p)
        kind = opts.get("pictures")
        try:
            width = max(64, min(640, int(opts.get("width", "600"))))
        except ValueError:
            width = 600
        return (kind if kind in ("320", "640") else None), width

    def send_picture(self, url):
        q = urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
        try:
            kind = q["k"][0] if q["k"][0] in ("320", "640") else "640"
            width = max(16, min(640, int(q.get("w", ["600"])[0])))
            src = q["u"][0]
            if not src.lower().startswith(("http://", "https://")):
                raise ValueError("bad picture address")
            if not target_allowed(urllib.parse.urlsplit(src).hostname or "", self.server.lan):
                raise ValueError("not allowed")
            data = picture(kind, width, src)
        except Exception as e:
            self.send_page(404, message("No picture", str(e)))
            return
        self.send_response(200)
        self.send_header("Content-Type", "image/x-apple-preferred")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def client_allowed(self):
        ip = ipaddress.ip_address(self.client_address[0])
        if ip.is_loopback or str(ip) in own_addresses():
            return True
        return self.server.lan and ip.is_private

    def do_GET(self):
        if not self.client_allowed():
            self.send_page(403, message("Not allowed", "This gateway only serves its own computer. "
                                        "Start it with --lan to serve the home network."))
            return
        url = self.path
        if url.startswith("/"):     # opened like a web site, not used as a proxy
            host = self.headers.get("Host", "")
            if url == "/" or not host or host.split(":")[0] in own_addresses() | {"localhost", ALIAS}:
                self.send_page(200, message("GSGate is running", "Set this address and port as the proxy in "
                                            "your browser. Then open any http:// page."))
                return
            url = "http://" + host + url
        if url.lower().startswith("https://"):     # Webber passes https addresses on to its proxy
            url = "http://" + url[8:]
        if not url.lower().startswith("http://"):
            self.send_page(400, message("Can't open this address", "Only http:// addresses work. "
                                        "The gateway looks for the https version itself."))
            return
        host = urllib.parse.urlsplit(url).hostname or ""
        if host == PIC_HOST:
            self.send_picture(url)
            return
        if not target_allowed(host, self.server.lan):
            self.send_page(403, message("Not allowed", "The gateway does not open addresses inside the home "
                                        "network. Switch the proxy off for those."))
            return
        try:
            final, ctype, body, is_page = cached_fetch(url)
        except urllib.error.HTTPError as e:
            self.send_page(e.code, message("The site answered with error %d" % e.code, "%s: %s" % (url, e.reason)))
            return
        except Exception as e:
            reason = getattr(e, "reason", e)
            self.send_page(502, message("Can't open the page", "%s: %s" % (url, reason)))
            return
        if is_page:
            if len(body) > MAX_FETCH:
                body = body[:MAX_FETCH]
            self.send_page(200, convert(decode(body, ctype), final, *self.client_wants()))
            return
        if len(body) > MAX_PASS:
            self.send_page(502, message("File too large", "The gateway passes files of up to 40 MB."))
            return
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    do_HEAD = do_GET

    def do_CONNECT(self):
        self.send_page(501, message("Not supported", "Ask for http:// addresses; the gateway fetches https itself."))

    def do_POST(self):
        self.send_page(501, message("Not supported", "Forms are not supported yet."))


def watch_stdin():
    """Stop when standard input closes: the program that started us (the emulator) has gone."""
    try:
        while sys.stdin.buffer.read(1):
            pass
    except (OSError, ValueError):
        pass
    os._exit(0)


def main():
    global ALIAS
    ap = argparse.ArgumentParser(description="Web gateway for Apple IIgs browsers")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--lan", action="store_true", help="serve other machines in the home network too")
    ap.add_argument("--bind", default="0.0.0.0", help="address to listen on (127.0.0.1: this computer only)")
    ap.add_argument("--alias", help="the address an emulated IIgs has for this computer")
    ap.add_argument("--watch-stdin", action="store_true", help="stop when standard input closes")
    ap.add_argument("--log", help="write the log to this file")
    args = ap.parse_args()
    if args.log:
        sys.stdout = sys.stderr = open(args.log, "w", buffering=1)
    ALIAS = args.alias
    try:
        srv = ThreadingHTTPServer((args.bind, args.port), Handler)
    except OSError as e:
        print("GSGate %s could not listen on %s port %d: %s" % (VERSION, args.bind, args.port, e))
        sys.exit(2)
    srv.lan = args.lan
    mine = sorted(a for a in own_addresses() if not a.startswith("127."))
    print("GSGate %s on port %d" % (VERSION, args.port))
    print("Proxy address for the IIgs browser: %s, port %d" % (ALIAS or (mine[0] if mine else "<this computer>"),
                                                               args.port))
    print("Serving: %s" % ("this computer and the home network" if args.lan else "this computer only (emulators)"))
    sys.stdout.flush()
    if args.watch_stdin:
        threading.Thread(target=watch_stdin, daemon=True).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
