# Launcher front end: plan for an Attract-Mode / Arcadeflow style look

Goal: the launcher should feel like a game front end (the Arcadeflow layout in your `~/.attract` setup), not like a
settings tool. Pictures first, almost no technical text, everything reachable with arrow keys or a game controller.

## 1. What Arcadeflow does that we want

Taken from your `~/.attract/layouts/Arcadeflow-17.7` and its README:

- **A horizontal scrolling grid of big pictures** (snapshots, box art or flyers), one row or several, with the selected
  one larger and in front. A short video preview plays over the selected picture.
- **A blurred, darkened background** made from the selected picture, so the screen changes colour with the selection.
- **A logo ("wheel") and a title** for the collection, not file names and options.
- **A second page with information** (description, year, maker, history text) and a large preview.
- **Favourites, filters, sorting and "type to search"**, all from the keyboard or a controller.
- **An overlay menu** on one button (launch, favourite, tags, edit information) instead of menu bars.
- **Full-screen by default**, adapts to any screen shape, and a separate layout for portrait screens.
- **Artwork kept in folders next to the games** (`snap`, `wheel`, `video`, `boxart`, `flyer`, `marquee`), shared by
  every front end. You already have these for the Apple IIgs in `~/Roms/apple2gs`.

## 2. What is "too technical" in the launcher today

- The window is a GS/OS-style tool window with a list and a command line shown on the right.
- Words such as slots, ramsize, bridge folder, hard disk 1 are in the main workflow.
- The menu bar, buttons and text are small and dense; nothing is a picture you can aim at.
- Adding or changing a profile means filling a long form.

## 3. The new look in short

1. **Home screen: "Games" (collections).** Big tiles with the cover or snapshot, a wheel logo, and the name. Scrolls
   sideways with animation. Background is the blurred cover of the selected tile.
2. **Press Return: launch.** Press a second button (or Tab): the **info page** with a larger picture, description,
   and a few friendly facts ("Sound: Mockingboard", "Internet: on"). The command line and the slot list move into an
   **Advanced** section that is closed by default.
3. **Overlay menu** (Space or the controller's menu button): Launch, Favourite, Edit, Add Cover, Duplicate, Delete.
4. **No menu bar in this view.** The macOS menu bar stays (it is the system's); on Windows and Linux the menu bar is
   hidden and opens with F10 or the mouse at the top edge.
5. **Full screen by default**, remembered; Esc leaves it.
6. The old windowed look stays available as a **Classic mode** (the skins we built), reached from the overlay menu,
   so nothing is lost.

## 4. Vocabulary

| Now | In the front end |
|---|---|
| Profile | **Game** or **Collection** (to decide, see questions) |
| ROM, hard disk 1-7, slots | hidden under **Advanced**; the editor offers **presets**: "Plain", "Internet", "Sound", "Printing", "Fast" |
| Library | **Disks and ROMs** (in Settings) |
| Command line | only in Advanced > Details |

## 5. Work packages

**A. Rendering foundation (needed first)**
- Text at several sizes, smooth, from a modern font (Figtree or Roboto, both open licences, are in Arcadeflow's fonts
  folder; I would check the licence file before including one). Today's font code makes one size per font and is
  pixel-style.
- Picture loading for PNG and JPG (SDL loads PNG and BMP; JPG needs the small public-domain `stb_image`), a texture
  cache that loads pictures in the background so scrolling never stutters.
- A cheap blur (draw the picture tiny, scale it up smoothly), gradients, vignette, rounded corners.
- A small animation helper (easing for scroll, scale, fade).
- Works at any window size and aspect ratio by laying out from percentages, not pixels.

**B. Home screen**
- The scrolling tile grid, selection animation, wheel and title, blurred background, bottom hint line.
- Keyboard, mouse (wheel scroll, click, double-click) and **game controller** (SDL gamepad).
- Type-to-search, sort (name, last played, most played), favourites row, filters.

**C. Info page and overlay menu**
- Large picture, description, facts. Advanced section with the current settings page.

**D. Friendly editor**
- A short flow: name, pick a picture, pick disks (drag and drop or Library), pick a **preset**. Everything else in
  Advanced. A first-run screen that finds the ROM, the disks and creates one collection.

**E. Artwork**
- Folder convention compatible with Attract-Mode: `snap`, `wheel`, `boxart`, `flyer`, `marquee`, `video` next to the
  game files; a collection finds `<name>.png` in those. Falls back to the generated cover.
- **Automatic snapshot**: EmuBuddy gets a hotkey or an option ("save a snapshot when I quit") that stores the screenshot
  as the collection's picture, so covers appear by playing.
- Optional later: look up pictures and descriptions on the internet (needs an account at a scraping site; ask first).

**F. Import from Attract-Mode**
- "Import from Attract-Mode" reads `~/.attract/romlists` and `emulators/*.cfg` and creates collections with your
  artwork paths (for example `Arcade Classics`, `Games Collection`, `system`, with the pictures in
  `~/Roms/apple2gs/video` and `wheel`). Nothing in `~/.attract` is changed.
- Optionally the reverse: EmuBuddy collections as a launchable entry for Attract-Mode (a `.sh` like your `system.sh`).

**G. Polish**
- Sounds for moving and launching (optional), an idle "attract" slideshow, a portrait layout, German texts.

## 6. Order and size (rough)

1. A + B first version (grid, blur, pictures, keys): the biggest part, about 2 to 3 weeks.
2. C, D (info page, overlay, friendly editor): about 1 to 2 weeks.
3. E, F (artwork folders, auto snapshot, importer): about 1 week.
4. Controller, sounds, idle slideshow, portrait, German: about 1 to 2 weeks.

## 7. Questions for you

1. **Video previews** (the short clips over the selected tile)? SDL has no video player; I would need to add a video
   decoder library (FFmpeg, large and with licence rules) or play pre-made frame sequences. Recommendation: pictures
   only at first, videos later if you want them.
2. **Keep the retro skins?** Recommendation: yes, as "Classic mode", and make the Arcadeflow-style view the default.
3. **Word** for the main items: Games, or Collections, or Systems?
4. **Internet artwork lookup** wanted, or only your own pictures and automatic snapshots?
5. Should the front end **replace** the profile list entirely, or should the profile editor stay reachable as before
   (as Advanced)? Recommendation: stay reachable, behind Edit.

## 8. Decisions and status

Your answers: pictures only (no video), keep the retro skins as Classic mode, wording left for later, start from the
Apple IIgs collection screenshots in `~/Roms/apple2gs` (adding and removing pictures per game later), the profile
editor stays reachable (Space > Edit).

**Done (first version):** font contexts so several font sizes work (Figtree from Arcadeflow's fonts folder, SIL Open
Font License); the Arcade view with the blurred background, the sliding row of covers, logo or name, friendly facts,
details page, search by typing, keyboard and mouse; picture folders compatible with Attract-Mode; the Space menu; dark
dialogs over the pictures; Classic view as a switch; import from Attract-Mode with the options of the start script;
Set/Remove Picture.

**Not done yet:** game controller input; sounds; idle slideshow; portrait layout; favourites and sorting; German texts;
the friendly editor with presets (the full editor is used for now); automatic snapshot when EmuBuddy quits;
JPG pictures (PNG and BMP load now).
