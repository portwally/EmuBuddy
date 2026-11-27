# Makes build/manual/manual.html from docs/manual/manual.html: the running
# footers (a named page for each chapter), each chapter's "In This Chapter"
# list, the contents and the index. The page numbers come from PAGES (a JSON
# file make_manual.sh writes from the PDF printed before); without it, "00".
import html, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "docs", "manual", "manual.html")
OUT = os.path.join(ROOT, "build", "manual", "manual.html")
DOCS = os.path.join(ROOT, "docs")

# index entries: (entry, words to look for on the pages)
INDEX = [
    ("About EmuBuddy", ["About EmuBuddy"]),
    ("Disks menu", ["Disks menu"]),
    ("Edit menu", ["Edit menu"]),
    ("File menu", ["File menu"]),
    ("Machine menu", ["Machine menu"]),
    ("Speed menu", ["Speed menu"]),
    ("View menu", ["View menu"]),
    ("Open Disk Image", ["Open Disk Image"]),
    ("Show Shared Folder", ["Show Shared Folder"]),
    ("private copy of a disk", ["private copy"]),
    ("Reset key", ["Reset key", "Control-Reset"]),
    ("Option key", ["Option"]),
    ("data folder", ["data folder"]),
    ("names of files", ["15 letters", "name changed", "names a ProDOS"]),
    ("Control Panels, in System 6", ["Control Panels"]),
    ("Apple key (Open-Apple)", ["Open-Apple"]),
    ("AppleShare", ["AppleShare"]),
    ("AppleSingle", ["AppleSingle"]),
    ("AppleTalk", ["AppleTalk"]),
    ("Applesoft BASIC", ["Applesoft"]),
    ("Attract-Mode, importing from", ["Attract-Mode"]),
    ("aux type", ["aux type"]),
    ("bridge folder", ["bridge folder", "Bridge folder", "host bridge"]),
    ("Check startup device!", ["Check startup device"]),
    ("Classic view", ["Classic view", "Classic View"]),
    ("command line", ["command line"]),
    ("Control Panel, of the IIgs", ["Control Panel"]),
    ("Copy Screen Text", ["Copy Screen Text"]),
    ("covers", ["covers", "a cover"]),
    ("CRT Effect", ["CRT Effect", "CRT effect"]),
    ("DHCP", ["DHCP"]),
    ("Disk Browser", ["Disk Browser"]),
    ("disk images", ["disk image"]),
    ("Disks and ROMs library", ["Disks and ROMs"]),
    ("downloads", ["download"]),
    ("dragging and dropping", ["drop", "Drop"]),
    ("Drives window", ["Drives window", "Drives Window"]),
    ("Drop Destination", ["Drop Destination"]),
    ("ejecting", ["Eject", "eject"]),
    ("emubuddy.config", ["emubuddy.config"]),
    ("EmuBuddy Files folder", ["EmuBuddy Files"]),
    ("EmuBuddy Launcher", ["Launcher"]),
    ("favourites", ["favourite", "Favourite"]),
    ("FFmpeg", ["FFmpeg"]),
    ("file types", ["file type"]),
    ("Full Screen", ["Full Screen", "full screen"]),
    ("game controller", ["game controller", "controller"]),
    ("games (set-ups)", ["a game", "games"]),
    ("Ghostscript", ["Ghostscript"]),
    ("Hard Reset", ["Hard Reset"]),
    ("hard disks", ["hard disk"]),
    ("helper, the", ["helper"]),
    ("HTTPS", ["HTTPS"]),
    ("ImageWriter II", ["ImageWriter"]),
    ("installing EmuBuddy", ["Installing", "xattr", "SmartScreen"]),
    ("Internet", ["Internet"]),
    ("joystick", ["joystick"]),
    ("keyboard layouts", ["German", "keyboard layout"]),
    ("LocalTalk", ["LocalTalk"]),
    ("Lock", ["Lock"]),
    ("Marinetti", ["Marinetti"]),
    ("memory", ["memory"]),
    ("menu bar", ["menu bar"]),
    ("Mockingboard", ["Mockingboard"]),
    ("Phasor", ["Phasor"]),
    ("modem", ["modem"]),
    ("Mount Shared Folder at Start", ["Mount Shared Folder at Start"]),
    ("network", ["network"]),
    ("network, starting up from the", ["startup server", "Starting up from the network"]),
    ("node number", ["node number"]),
    ("partitions", ["partition"]),
    ("Paste Text", ["Paste Text"]),
    ("Pause", ["Pause", "PAUSED"]),
    ("PDF", ["PDF"]),
    ("pictures of games", ["Set Picture", "Artwork Folder", "artwork"]),
    ("portrait screens", ["portrait", "Portrait"]),
    ("PostScript printer", ["PostScript"]),
    ("Printer window", ["Printer window"]),
    ("printing", ["print", "Print"]),
    ("ProDOS", ["ProDOS"]),
    ("proxy", ["proxy", "Proxy"]),
    ("Python", ["Python"]),
    ("quitting", ["Quit"]),
    ("recent disks", ["recently used", "recent disks"]),
    ("Record Video", ["Record Video", "recording"]),
    ("ROM file", ["ROM file", "ROM 03", "ROM 01"]),
    ("router, AppleTalk", ["AppleTalk router", "TashRouter"]),
    ("ROM Required", ["ROM Required"]),
    ("scanlines", ["Scanlines", "scanlines"]),
    ("screenshots", ["Screenshot", "screenshot"]),
    ("SCSI card", ["SCSI"]),
    ("Share a Folder", ["Share a Folder", "Share Folder"]),
    ("shared folders", ["shared folder", "Shared folder"]),
    ("Second Sight VGA card", ["Second Sight"]),
    ("AppleSqueezer", ["AppleSqueezer"]),
    ("ShrinkIt archives", ["ShrinkIt"]),
    ("skins", ["skin"]),
    ("slots", ["slot ", "Slots"]),
    ("Soft Reset", ["Soft Reset"]),
    ("sounds, of the Launcher", ["Sounds"]),
    ("speech chips", ["speech chip"]),
    ("speed", ["MHz"]),
    ("startup disk", ["startup disk"]),
    ("status bar", ["status bar"]),
    ("System 6", ["System 6"]),
    ("UNABLE TO LOAD PRODOS", ["UNABLE TO LOAD PRODOS"]),
    ("Uthernet II", ["Uthernet"]),
    ("Video Overlay Card", ["Video Overlay"]),
    ("web gateway", ["web gateway", "Web gateway"]),
    ("Webber", ["Webber"]),
    ("window size", ["Window Size", "window size"]),
    ("Your Card", ["Your Card"]),
    ("ZipGS", ["ZipGS"]),
]

pages = {}
if os.environ.get("PAGES") and os.path.exists(os.environ["PAGES"]):
    pages = json.load(open(os.environ["PAGES"]))
marks = pages.get("marks", {})
texts = pages.get("texts", [])          # each page's text, from the first PDF
def pg(key):
    return str(marks.get(key, "00"))

s = open(SRC, encoding="utf-8").read()

# the chapters, their sections; marks for the pages
chapters = []
def chapter(m):
    sid, label, title, inner = m.group(1), m.group(2), m.group(3), m.group(4)
    secs = []
    def h2(hm):
        n = len(secs)
        key = f"{sid}-s{n}"
        secs.append((key, re.sub(r"<[^>]+>", "", hm.group(1))))
        return f'<h2 id="{key}"><span class="mk">@@{key}@@</span>{hm.group(1)}</h2>'
    inner = re.sub(r"<h2>(.*?)</h2>", h2, inner)
    chapters.append((sid, label, title, secs))
    inch = ""
    if secs and "<!--INCH-->" in inner:
        inch = '<div class="inch"><div class="lbl">In This Chapter</div><ul>' + "".join(
            f'<li><span>{html.escape(t)}</span><span class="pg">{pg(k)}</span></li>' for k, t in secs) + "</ul></div>"
    inner = inner.replace("<!--INCH-->", inch)
    inner = inner.replace('<div class="opener">', f'<div class="opener"><span class="mk">@@{sid}@@</span>', 1)
    return (f'<section class="{m.group(0).split(chr(34))[1]}" id="{sid}" style="page: p-{sid}" '
            f'data-label="{label}" data-title="{title}">{inner}</section>')
s = re.sub(r'<section class="chapter[^"]*" id="([^"]+)" data-label="([^"]+)" data-title="([^"]+)">(.*?)</section>',
           chapter, s, flags=re.S)

# the running footers: the page number outside, the chapter inside
css = ["<style>"]
for sid, label, title, secs in chapters:
    run = html.escape(title if sid == "index" else f"{label}: {title}").replace('"', "'")
    foot = "font-family: AG; font-weight: 300; font-size: 8.2pt; color: #5a5650;"
    css.append(f"@page p-{sid}:right {{ @bottom-right {{ content: counter(page); {foot} font-weight: 400; color: #1a1917; }}"
               f" @bottom-left {{ content: \"{run}\"; {foot} }} }}")
    css.append(f"@page p-{sid}:left {{ @bottom-left {{ content: counter(page); {foot} font-weight: 400; color: #1a1917; }}"
               f" @bottom-right {{ content: \"{run}\"; {foot} }} }}")
css.append("</style>")
s = s.replace("<!--PAGES-->", "\n".join(css))

# the contents
toc = []
for sid, label, title, secs in chapters:
    n = "" if sid == "index" else label
    toc.append(f'<a class="ent" href="#{sid}"><span class="n">{n}</span><span class="t">{html.escape(title)}</span><span class="pg">{pg(sid)}</span></a>')
    if secs and not sid.startswith("app"):
        toc.append('<div class="subs">' + "".join(
            f'<a class="sub" href="#{k}"><span>{html.escape(t)}</span><span class="pg">{pg(k)}</span></a>' for k, t in secs) + "</div>")
s = s.replace("<!--TOC-->", "\n".join(toc))

# the index: the pages where each entry's words are, from the preface on (not the index itself)
first = int(marks.get("preface", 0) or 0)
last = int(marks.get("index", 0) or 0)
ix = []
letter = ""
for entry, words in sorted(INDEX, key=lambda e: e[0].lstrip("0123456789").lower()):
    found = []
    for p, t in enumerate(texts, 1):
        if first and p < first:
            continue
        if last and p >= last:
            continue
        flat = re.sub(r"-\n", "", t).replace("\n", " ")
        if any(w in flat for w in words):
            found.append(p)
    nums = ", ".join(str(p) for p in found[:8]) if texts else "00, 00, 00"
    l = entry.lstrip("0123456789")[:1].upper()
    if entry[0].isdigit():
        l = "#"
    if l != letter:
        ix.append(f'<div class="letter">{l}</div>')
        letter = l
    if nums:
        ix.append(f'<div class="ix">{html.escape(entry)}, <span class="pp">{nums}</span></div>')
s = s.replace("<!--INDEX-->", "\n".join(ix))

# paths: the pictures and the style sheet, from build/manual
s = s.replace('src="img/', f'src="file://{DOCS}/manual/img/').replace('href="manual.css"', f'href="file://{DOCS}/manual/manual.css"')
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write(s)
print("build/manual/manual.html:", len(chapters), "chapters")
