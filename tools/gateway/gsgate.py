#!/usr/bin/env python3
"""GSGate: a web gateway for Apple IIgs browsers.

It works as an ordinary HTTP proxy. The IIgs browser asks it for a page; it fetches the page (HTTP or
HTTPS), throws away scripts, styles and everything else an old browser can't use, and answers with a
small, strict HTML page. Files that are not pages are passed through unchanged.

    python3 gsgate.py                 listen on port 8080, serve this computer only (emulators)
    python3 gsgate.py --lan           also serve other machines in the home network (a real IIgs)
    python3 gsgate.py --port 6502

On a Raspberry Pi or a NAS: see README.md next to this file (gsgate.service, Dockerfile).

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

VERSION = "0.7"
FORMAT = "2"                    # version of the mini page format (2: forms and tables, for a browser that asks)
MAX_FETCH = 3 * 1024 * 1024     # largest page we read
MAX_PASS = 40 * 1024 * 1024     # largest download we pass through
MAX_PAGE = 48 * 1024            # largest page we send to the IIgs
TIMEOUT = 20
MAX_COLS = 6                    # columns of a table we lay out; wider ones become lines of text
MAX_PROBE = 24                  # pictures whose size we look up for one page (what the browser shows of them)
PROBE_TIME = 6                  # seconds we wait for those sizes, all pictures together
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


def attr(text):
    """Text as the value of an attribute."""
    return esc(text).replace('"', "&quot;")


# How wide Peel draws the characters ' ' to '~' (the IIgs system font in 640 mode, read out of Peel itself).
# The gateway needs them to give a table's columns their widths.
CHAR_W = [4, 6, 8, 8, 8, 8, 9, 6, 7, 7, 7, 8, 5, 8, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4, 5, 7, 8, 7, 8,
          8, 8, 8, 8, 8, 7, 7, 8, 8, 8, 8, 9, 8, 9, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 8, 8, 8, 6, 8, 6, 8, 8,
          5, 8, 8, 8, 8, 8, 7, 8, 8, 6, 7, 8, 8, 9, 8, 8, 8, 8, 7, 8, 8, 8, 9, 9, 8, 8, 8, 8, 6, 8, 9]


def text_width(text, bold=False):
    """Pixels the text takes in Peel's page (bold: a pixel more for each character)."""
    return sum(CHAR_W[ord(c) - 32] if 32 <= ord(c) < 127 else 8 for c in text) + (len(text) if bold else 0)


# ---- the page converter ----

SKIP = {"script", "style", "noscript", "svg", "canvas", "iframe", "object", "embed", "video", "audio",
        "template", "button", "select", "textarea", "dialog", "map", "picture", "source", "head"}
CHROME = {"nav", "header", "footer", "aside"}       # page furniture: kept out of the text, links collected
BLOCK = {"p", "div", "section", "article", "main", "figure", "figcaption", "address", "details",
         "summary", "dl", "dt", "dd", "fieldset", "table", "tbody", "thead", "tfoot", "caption", "form"}
TEXT_INPUT = {"text", "search", "email", "url", "tel", "number"}
VOID = {"br", "hr", "img", "input", "meta", "link", "area", "base", "col", "wbr", "source", "track", "embed"}
INLINE = {"b": "B", "strong": "B", "i": "I", "em": "I", "cite": "I", "u": "U", "ins": "U"}


class Converter(HTMLParser):
    def __init__(self, base, main_only, pics=None, pic_width=600, forms=False, tables=False, flat=()):
        super().__init__(convert_charrefs=True)
        self.base = base
        self.pics = pics            # None: no pictures (Webber); "640": pictures for the page window
        self.pic_width = pic_width
        self.want_forms = forms     # the browser can show a form's text box (Peel 1.1)
        self.want_tables = tables   # and lay out a table whose cells have widths
        self.flat = flat            # tables (by number) that are not simple enough: lines of text, as before
        self.tcount = 0             # tables met so far
        self.tbl = None             # the table being collected
        self.bad_tables = set()     # tables that turned out not simple enough
        self.forms = []             # every form met
        self.form = None            # the open one (its number)
        self.pending = []           # (address, alt) of pictures whose size the page does not state
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
        if self.tbl is not None:    # in a table that is being collected: a new line in its cell
            if self.tbl["cell"] is not None:
                self.cell_break()
            else:
                self.line = []
            return
        text = re.sub(r"[ \t\r\n]+", " ", "".join(self.line)).strip()
        self.line = []
        if "<IMG" not in text and "<FORMINPUT" not in text and not re.sub(r"<[^>]+>", "", text).strip(" |*-"):
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
        if tag == "table":
            self.tcount += 1
        if tag == "form":
            self.form_end()         # (forms are not nested)
            if self.want_forms and not self.skip:
                self.form_start(a)
        elif self.form is not None and tag in ("input", "select", "option", "textarea", "button"):
            self.form_field(tag, a)
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
        if self.tbl is not None:
            if self.table_tag(tag, a):
                return
        elif tag == "table" and self.want_tables and self.tcount not in self.flat:
            self.flush()
            self.tbl = {"idx": self.tcount, "rows": [], "row": None, "cell": None, "bad": False, "depth": 0}
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
            if self.tbl is None:
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
        try:
            w, h = int(a.get("width", "")), int(a.get("height", ""))
        except ValueError:
            # The page does not say how big the picture is. The mini format promises a size on every IMG,
            # so the picture itself is looked at, after the page has been read (fill_pictures).
            if len(self.pending) >= MAX_PROBE:
                return None
            self.pending.append((url, alt))
            return "<IMG PENDING=%d>" % (len(self.pending) - 1)
        return self.pic_tag(url, alt, w, h)

    def pic_tag(self, url, alt, w, h):
        """The IMG tag for a picture of w x h pixels, or None for one too small to be worth showing."""
        if w < 24 or h < 24:
            return None                 # tracking pixels, bullets and spacers
        tw, th = gspic.target_size(w, h, self.pics, self.pic_width)
        # WIDTH and HEIGHT are in square "web" pixels; the browser turns them into screen lines
        size = ' WIDTH="%d" HEIGHT="%d"' % (tw, round(th / gspic.ASPECT[self.pics]))
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
        if top == "form":
            self.form_end()
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
            if self.tbl is not None and self.table_close(top):
                pass
            elif top in ("h1", "h2", "h3"):
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

    # -- tables --
    def cell_break(self):
        """A new line inside the cell being collected."""
        if "".join(self.line).strip() and self.line[-1] != "<BR>":
            self.line.append("<BR>")

    def cell_end(self):
        t = self.tbl
        if t["cell"] is not None:
            text = re.sub(r"[ \t\r\n]+", " ", "".join(self.line))
            text = re.sub(r"^(\s|<BR>)+|(\s|<BR>)+$", "", text)
            if "<IMG" not in text and not re.sub(r"<[^>]+>", "", text).strip():
                text = ""           # (only an empty link, or nothing)
            t["cell"]["html"] = re.sub(r"\s*<BR>\s*", "<BR>", text)
            t["cell"] = None
        self.line = []

    def table_tag(self, tag, a):
        """A tag that opens inside the table being collected. True: it is dealt with."""
        t = self.tbl
        if tag == "table":          # a table in a table: not a simple one
            t["bad"] = True
            t["depth"] += 1
            return True
        if t["depth"]:
            return True

        def span(name):
            try:
                return max(1, min(50, int(a.get(name) or 1)))
            except ValueError:
                return 1

        if tag == "tr":
            self.cell_end()
            t["row"] = []
            t["rows"].append(t["row"])
            return True
        if tag in ("td", "th"):
            self.cell_end()
            if t["row"] is None:
                t["row"] = []
                t["rows"].append(t["row"])
            if span("rowspan") > 1:
                t["bad"] = True
            t["cell"] = {"th": tag == "th", "span": span("colspan"), "html": ""}
            t["row"].append(t["cell"])
            return True
        if tag == "li":
            self.cell_break()
            self.line.append("* ")
            return True
        if tag in BLOCK or tag in ("ul", "ol", "menu", "pre", "blockquote", "h1", "h2", "h3", "h4", "h5", "h6"):
            self.cell_break()
            return True
        return False

    def table_close(self, top):
        t = self.tbl
        if top == "table":
            if t["depth"]:
                t["depth"] -= 1
            else:
                self.table_end()
            return True
        if t["depth"]:
            return True
        if top == "tr":
            self.cell_end()
            t["row"] = None
            return True
        if top in ("td", "th"):
            self.cell_end()
            return True
        if top in BLOCK or top in ("ul", "ol", "menu", "li", "pre", "blockquote", "h1", "h2", "h3", "h4", "h5", "h6"):
            self.cell_break()
            return True
        return False

    def table_end(self):
        t = self.tbl
        self.cell_end()
        self.tbl = None
        pieces = None if t["bad"] else table_markup(t["rows"], self.pic_width)
        if pieces is None:          # not simple enough: the page is read again with this table as text
            self.bad_tables.add(t["idx"])
            return
        for piece in pieces:
            self.emit(piece)

    # -- forms --
    def form_start(self, a):
        url = urllib.parse.urljoin(self.base, (a.get("action") or "").strip())
        f = {"action": urllib.parse.urldefrag(url)[0], "method": (a.get("method") or "get").strip().lower(),
             "hidden": [], "texts": [], "submit": None, "select": None, "bad": False, "inline": False}
        self.forms.append(f)
        self.form = len(self.forms) - 1
        if self.visible() and self.tbl is None:
            self.flush()
            self.emit("<FORMSTART %d>\n" % self.form)
            f["inline"] = True

    def select_end(self, f):
        sel = f["select"]
        f["select"] = None
        if sel and sel["name"]:     # (its field keeps the place the select has in the form)
            value = sel["chosen"] if sel["chosen"] is not None else sel["first"]
            f["hidden"][sel["at"]] = (sel["name"], value)

    def form_field(self, tag, a):
        f = self.forms[self.form]
        name = a.get("name")
        if tag == "input":
            kind = (a.get("type") or "text").strip().lower()
            if kind in TEXT_INPUT:
                if not name:
                    return
                label = a.get("placeholder") or a.get("aria-label") or a.get("title") or ""
                try:
                    size = int(a.get("size") or 0)
                except ValueError:
                    size = 0
                f["texts"].append({"name": name, "value": a.get("value") or "", "size": size, "label": label,
                                   "search": kind == "search"})
                if f["inline"] and len(f["texts"]) == 1 and self.visible() and self.tbl is None:
                    self.line.append("<FORMINPUT %d>" % self.form)
            elif kind == "hidden":
                if name:
                    f["hidden"].append((name, a.get("value") or ""))
            elif kind in ("checkbox", "radio"):
                if name and "checked" in a:
                    f["hidden"].append((name, a.get("value") or "on"))
            elif kind == "submit":
                if f["submit"] is None:
                    f["submit"] = (name, a.get("value") or "")
            elif kind in ("password", "file"):
                f["bad"] = True     # a login, an upload: not a search box
        elif tag == "button":
            if (a.get("type") or "submit").lower() == "submit" and f["submit"] is None:
                f["submit"] = (name, a.get("value") or "")
        elif tag == "textarea":
            f["bad"] = True
        elif tag == "select":
            self.select_end(f)
            f["select"] = {"name": name, "first": None, "chosen": None, "at": len(f["hidden"])}
            if name:
                f["hidden"].append((name, None))
        elif tag == "option" and f["select"]:
            if "value" not in a:
                f["bad"] = True     # (its value would be its text)
                return
            if f["select"]["first"] is None:
                f["select"]["first"] = a["value"]
            if "selected" in a:
                f["select"]["chosen"] = a["value"]

    def form_end(self):
        if self.form is None:
            return
        f = self.forms[self.form]
        self.select_end(f)
        if f["inline"]:
            self.flush()
            self.emit("<FORMEND %d>\n" % self.form)
        self.form = None

    def form_tags(self, f):
        """(the FORM tag with its hidden fields, the INPUT tag) of a form with one text box, or None."""
        if f["bad"] or len(f["texts"]) != 1 or f["method"] not in ("get", "post"):
            return None
        if not f["action"].lower().startswith(("http://", "https://")):
            return None
        box = f["texts"][0]
        if not (f["submit"] or box["search"] or box["label"].strip()):
            return None             # (a box without a button or a word about it is not meant for the reader)
        hidden = [(n, v) for n, v in f["hidden"] if v is not None]
        if f["submit"] and f["submit"][0]:
            hidden.append(f["submit"])
        if f["method"] == "post":   # the gateway sends it: the form's own address is the first field
            action = "http://%s/post" % PIC_HOST
            hidden.insert(0, ("u", f["action"]))
        else:
            action = self.link(f["action"].split("?")[0])
            if not action:
                return None
        head = '<FORM ACTION="%s">' % action
        head += "".join('<INPUT TYPE="hidden" NAME="%s" VALUE="%s">' % (attr(n), attr(v)) for n, v in hidden)
        size = max(10, min(40, box["size"] or 24))
        box_tag = '<INPUT NAME="%s" VALUE="%s" SIZE="%d"' % (attr(box["name"]), attr(box["value"]), size)
        if box["label"].strip():
            box_tag += ' PLACEHOLDER="%s"' % attr(re.sub(r"\s+", " ", box["label"]).strip()[:60])
        return head, box_tag + ">"

    def fill_forms(self, body):
        """The forms' place holders in the page become FORM and INPUT tags (or nothing, for a form that
        is not a search box). Returns the page and the forms that are not in its text (page furniture)."""
        top = []
        for n, f in enumerate(self.forms):
            tags = self.form_tags(f)
            start, box, end = "<FORMSTART %d>\n" % n, "<FORMINPUT %d>" % n, "<FORMEND %d>\n" % n
            if tags and box in body:
                body = body.replace(start, tags[0] + "\n").replace(box, tags[1])
                body = body.replace(end, "</FORM>\n")
                continue
            body = body.replace(start, "").replace(box, "").replace(end, "")
            if tags and len(top) < 2:
                label = (f["submit"][1] if f["submit"] else "") or "Search"
                label = re.sub(r"\s+", " ", label).strip()[:30]
                block = "%s\n<P>%s: %s</P>\n</FORM>\n" % (tags[0], esc(label), tags[1])
                if block not in top:
                    top.append(block)
        return body, top

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
        if self.tbl is not None:    # (a table the page never closed)
            self.table_end()
        self.form_end()
        self.flush()
        while self.lists:
            self.emit("</%s>\n" % self.lists.pop())
        title = re.sub(r"\s+", " ", plain(self.title)).strip() or urllib.parse.urlsplit(url).netloc
        body, top_forms = self.fill_forms("".join(self.out))
        body = re.sub(r"<(P|LI)>\s*</\1>\n", "", body)
        parts = ["<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n<BODY>\n" % esc(title)]
        if "<H1>" not in body[:600]:
            parts.append("<H1>%s</H1>\n" % esc(title))
            parts.extend(top_forms)
        elif top_forms:             # (the page has its own heading: the search box comes after that one)
            at = body.find("</H1>\n") + 6
            body = body[:at] + "".join(top_forms) + body[at:]
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


def table_markup(rows, total=600):
    """A collected table as TABLE, TR and TD tags, each cell with its width in pixels, in pieces (a row
    each). None: it is not a simple table (too many columns, too much in a cell, a picture of unknown
    size), and is better shown as lines of text."""
    rows = [r for r in rows if any(c["html"] for c in r)]
    if not rows:
        return []
    ncols = max(sum(c["span"] for c in r) for r in rows)
    used = [False] * ncols          # columns with nothing in them are left out
    for r in rows:
        col = 0
        for c in r:
            if c["html"]:
                used[col:col + c["span"]] = [True] * len(used[col:col + c["span"]])
            col += c["span"]
    if not all(used):
        kept = []
        for r in rows:
            col, row = 0, []
            for c in r:
                span = sum(used[col:col + c["span"]])
                col += c["span"]
                if span:
                    row.append(dict(c, span=span))
            kept.append(row)
        rows, ncols = kept, sum(used)
    if ncols < 2 or ncols > MAX_COLS:
        return None
    low = [16] * ncols              # what each column needs at least (its longest word), and would like
    want = [16] * ncols             # (its longest line)
    for r in rows:
        col = 0
        for c in r:
            text = c["html"]
            if "<IMG PENDING" in text or text.count("<BR>") > 12:
                return None
            pics = [int(w) for w in re.findall(r'<IMG [^>]*WIDTH="(\d+)"', text)]
            lines = [html.unescape(re.sub(r"<[^>]+>", "", part)).strip()
                     for part in re.sub(r"<IMG[^>]*>", "", text).split("<BR>")]
            if sum(len(line) for line in lines) > 400:
                return None         # a page laid out with a table, not a table of facts
            bold = c["th"] or "<B>" in text
            c_low = max([text_width(w, bold) for line in lines for w in line.split()] + pics + [0]) + 10
            c_want = max([text_width(line, bold) for line in lines] + pics + [0]) + 10
            if c["span"] == 1 and col < ncols:
                low[col] = max(low[col], c_low)
                want[col] = max(want[col], c_want)
            elif max(pics + [0]) + 10 > total:
                return None
            col += c["span"]
    if sum(low) > total:
        return None
    if sum(want) <= total:
        widths = want
    else:
        spare, more = total - sum(low), [w - l for w, l in zip(want, low)]
        widths = [l + spare * m // sum(more) for l, m in zip(low, more)]
    out = ["<TABLE>\n"]
    for r in rows:
        col, cells = 0, []
        for c in r:
            if col >= ncols:
                break
            span = min(c["span"], ncols - col)
            tag = "TH" if c["th"] else "TD"
            cells.append('<%s WIDTH="%d">%s</%s>' % (tag, sum(widths[col:col + span]), c["html"], tag))
            col += span
        out.append("<TR>%s</TR>\n" % "".join(cells))
    out.append("</TABLE>\n")
    return out


def convert(text, url, pics=None, pic_width=600, allow_lan=False, forms=False, tables=False):
    """A web page as mini HTML (format 2; forms and tables only for a browser that asks for them)."""
    low = text.lower()
    main_only = bool(re.search(r"<(main|article)[\s>]|role=[\"']main[\"']", low))
    for attempt in (main_only, False):
        flat = set()
        for again in range(6):      # (a table that is not simple is known only at its end)
            # tables in tables, level after level: after five readings all of them are text
            c = Converter(url, attempt, pics, pic_width, forms, tables and again < 5, frozenset(flat))
            try:
                c.feed(text)
                c.close()
            except Exception:       # a broken page must not stop the gateway
                pass
            page = c.result(url)
            if not c.bad_tables:
                break
            flat |= c.bad_tables
        # if "main only" left almost nothing, the page marks its content differently: take everything
        if not attempt or len(re.sub(r"<[^>]+>", "", "".join(c.out))) > 200:
            break
    return fill_pictures(page, c, allow_lan)


def fill_pictures(page, c, allow_lan):
    """The pictures the page gave no size for: each becomes an IMG with its real size, or its text."""
    if not c.pending:
        return page
    sizes = picture_sizes([url for url, _alt in c.pending], allow_lan)

    def one(m):
        url, alt = c.pending[int(m.group(1))]
        size = sizes[int(m.group(1))]
        tag = c.pic_tag(url, alt, *size) if size else None
        return tag or ("[%s]" % esc(alt) if alt else "")

    page = re.sub(r"<IMG PENDING=(\d+)>", one, page)
    return re.sub(r"<(P|LI)>\s*</\1>\n", "", page)     # paragraphs that held only a picture we left out


def picture_sizes(urls, allow_lan):
    """(width, height) of each picture, or None. They are fetched at the same time; whatever has not
    come after PROBE_TIME seconds counts as unknown. The fetched pictures stay in the cache for the
    request that asks for them converted."""
    found = {}

    def one(url):
        try:
            if target_allowed(urllib.parse.urlsplit(url).hostname or "", allow_lan):
                found[url] = gspic.picture_size(cached_fetch(url)[2])
        except Exception:
            pass

    threads = [threading.Thread(target=one, args=(u,), daemon=True) for u in dict.fromkeys(urls)]
    for t in threads:
        t.start()
    deadline = time.time() + PROBE_TIME
    for t in threads:
        t.join(max(0.0, deadline - time.time()))
    return [found.get(u) for u in urls]


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
        if ip.is_loopback or ip.is_multicast or ip.is_unspecified:
            return False
        # (a link-local address is one of the home network's too: a Mac's name.local has an fe80:: one)
        if (ip.is_private or ip.is_link_local) and not allow_lan:
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


def fetch(url, data=None):
    """(final url, content type, body bytes, is_page). http is tried first; redirects to https are followed.
    data: a form's fields, to send them with POST."""
    last = None
    tries = [url]
    parts = urllib.parse.urlsplit(url)
    local = bool(ALIAS) and parts.hostname == ALIAS
    if local:                       # this computer, as the emulated IIgs calls it
        tries = [urllib.parse.urlunsplit(parts._replace(netloc="127.0.0.1" + (":%d" % parts.port if parts.port else "")))]
    elif url.lower().startswith("http://"):
        tries.append("https://" + url[7:])
    for u in tries:
        headers = {"User-Agent": AGENT, "Accept": "text/html,*/*;q=0.5", "Accept-Language": "en,de;q=0.7"}
        if data is not None:
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        req = urllib.request.Request(u, data=data, headers=headers)
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
_cache_lock = threading.Lock()  # (requests are served, and picture sizes looked up, in threads)


def cached_fetch(url):
    now = time.time()
    with _cache_lock:
        for k in [k for k, v in _cache.items() if now - v[0] > 120]:
            _cache.pop(k, None)
        hit = _cache.get(url)
    if hit:
        return hit[1]
    result = fetch(url)
    if len(result[2]) <= MAX_FETCH:
        with _cache_lock:
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
        """What the browser can show, from the header Peel sends (a browser without it gets text only):
        X-GSGate-Client: pictures=640; width=600; forms=1; tables=1"""
        opts = dict(p.strip().split("=", 1) for p in self.headers.get("X-GSGate-Client", "").split(";") if "=" in p)
        kind = opts.get("pictures")
        try:
            width = max(64, min(640, int(opts.get("width", "600"))))
        except ValueError:
            width = 600
        return {"pics": kind if kind in ("320", "640") else None, "pic_width": width,
                "forms": opts.get("forms") == "1", "tables": opts.get("tables") == "1"}

    def send_post(self, url):
        """A form sent with POST. The browser asks with GET (all it can do):
        http://gsgate.invalid/post?u=<the form's address>&<its fields>"""
        fields = urllib.parse.parse_qsl(urllib.parse.urlsplit(url).query, keep_blank_values=True)
        if not fields or fields[0][0] != "u" or not fields[0][1].lower().startswith(("http://", "https://")):
            self.send_page(400, message("Can't send this form", "The form's address is missing."))
            return
        target = fields[0][1]
        if not target_allowed(urllib.parse.urlsplit(target).hostname or "", self.server.lan):
            self.send_page(403, message("Not allowed", "The gateway does not send forms to addresses inside "
                                        "the home network."))
            return
        try:
            result = fetch(target, urllib.parse.urlencode(fields[1:]).encode("ascii"))
        except urllib.error.HTTPError as e:
            self.send_page(e.code, message("The site answered with error %d" % e.code, "%s: %s" % (target, e.reason)))
            return
        except Exception as e:
            self.send_page(502, message("Can't send the form", "%s: %s" % (target, getattr(e, "reason", e))))
            return
        self.send_result(*result)

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
            if urllib.parse.urlsplit(url).path == "/post":
                self.send_post(url)
            else:
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
        self.send_result(final, ctype, body, is_page)

    def send_result(self, final, ctype, body, is_page):
        """What was fetched goes to the browser: a page converted, a file as it is."""
        if is_page:
            if len(body) > MAX_FETCH:
                body = body[:MAX_FETCH]
            self.send_page(200, convert(decode(body, ctype), final, allow_lan=self.server.lan,
                                        **self.client_wants()))
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
        self.send_page(501, message("Not supported", "The gateway takes forms only from Peel (a search box "
                                    "it has put into a page)."))


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
    if not ALIAS and (os.path.exists("/.dockerenv") or os.path.exists("/run/.containerenv")):
        # in a container the gateway's own address is one nobody outside can use
        print("Proxy address for the IIgs browser: the address of the machine this container runs on, "
              "and the port published for it (%d unless you chose another)" % args.port)
    else:
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
