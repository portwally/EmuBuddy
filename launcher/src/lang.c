/* The Launcher's texts in German; see lang.h. The left column is the English
 * text exactly as the program has it, the right one what is shown instead.
 * A text that is not in the table stays English. The printf directives (%s,
 * %d ...) must be the same and in the same order on both sides: the test
 * tests/test_lang.c checks that. */
#include "lang.h"
#include <string.h>

static const struct { const char *en, *de; } g_text[] = {
	/* menus */
	{ "File", "Ablage" },
	{ "Edit", "Bearbeiten" },
	{ "Profile", "Profil" },
	{ "View", "Darstellung" },
	{ "Skin", "Skin" },
	{ "Help", "Hilfe" },
	{ "New Profile", "Neues Profil" },
	{ "Duplicate Profile", "Profil duplizieren" },
	{ "Delete Profile...", "Profil löschen..." },
	{ "Show Profiles Folder", "Profilordner zeigen" },
	{ "Quit", "Beenden" },
	{ "Cut", "Ausschneiden" },
	{ "Copy", "Kopieren" },
	{ "Paste", "Einsetzen" },
	{ "Select All", "Alles auswählen" },
	{ "Window Size 0.8x", "Fenstergröße 0,8x" },
	{ "Window Size 1x", "Fenstergröße 1x" },
	{ "Window Size 1.25x", "Fenstergröße 1,25x" },
	{ "Window Size 1.5x", "Fenstergröße 1,5x" },
	{ "Window Size 2x", "Fenstergröße 2x" },
	{ "Window Size 3x", "Fenstergröße 3x" },
	{ "Full Screen", "Vollbild" },
	{ "Portrait Window", "Hochformat-Fenster" },
	{ "Landscape Window", "Querformat-Fenster" },
	{ "Sounds", "Töne" },
	{ "Idle Slideshow", "Diashow im Leerlauf" },
	{ "Sort: A-Z", "Sortierung: A-Z" },
	{ "Sort: Favourites First", "Sortierung: Favoriten zuerst" },
	{ "Sort: Last Played", "Sortierung: Zuletzt gespielt" },
	{ "Sort: Most Played", "Sortierung: Meistgespielt" },
	{ "Favourites Only", "Nur Favoriten" },
	{ "Arcade View", "Arcade-Ansicht" },
	{ "Classic View", "Klassische Ansicht" },
	{ "Play", "Starten" },
	{ "Edit...", "Bearbeiten..." },
	{ "Favourite", "Favorit" },
	{ "Library...", "Bibliothek..." },
	{ "About EmuBuddy Launcher", "Über EmuBuddy Launcher" },

	/* the menu of the Arcade view */
	{ "Menu", "Menü" },
	{ "Details", "Details" },
	{ "Sort", "Sortierung" },
	{ "Sort: %s", "Sortierung: %s" },
	{ "Layout: %s", "Anordnung: %s" },
	{ "Pictures: %s", "Bilder: %s" },
	{ "Carousel", "Karussell" },
	{ "1 Row", "1 Reihe" },
	{ "2 Rows", "2 Reihen" },
	{ "3 Rows", "3 Reihen" },
	{ "Box Art", "Schachteln" },
	{ "Screenshots", "Bildschirmfotos" },
	{ "Layout: Carousel", "Anordnung: Karussell" },
	{ "Layout: 1 Row", "Anordnung: 1 Reihe" },
	{ "Layout: 2 Rows", "Anordnung: 2 Reihen" },
	{ "Layout: 3 Rows", "Anordnung: 3 Reihen" },
	{ "Pictures: Box Art", "Bilder: Schachteln" },
	{ "Pictures: Screenshots", "Bilder: Bildschirmfotos" },
	{ "A-Z", "A-Z" },
	{ "Favourites first", "Favoriten zuerst" },
	{ "Last played", "Zuletzt gespielt" },
	{ "Most played", "Meistgespielt" },
	{ "Add to Favourites", "Zu den Favoriten" },
	{ "Remove from Favourites", "Aus den Favoriten entfernen" },
	{ "Added to Favourites", "Zu den Favoriten hinzugefügt" },
	{ "Removed from Favourites", "Aus den Favoriten entfernt" },
	{ "New Game...", "Neues Spiel..." },
	{ "Duplicate", "Duplizieren" },
	{ "Delete...", "Löschen..." },
	{ "Disks and ROMs...", "Disks und ROMs..." },
	{ "Set Picture...", "Bild wählen..." },
	{ "Remove Picture...", "Bild entfernen..." },
	{ "Artwork Folder...", "Bilderordner..." },
	{ "Import from Attract-Mode...", "Aus Attract-Mode importieren..." },
	{ "Sounds on/off", "Töne ein/aus" },
	{ "Language", "Sprache" },
	{ "About", "Über" },

	/* the Arcade view */
	{ "No games yet. Press Space to add one.", "Noch keine Spiele. Mit der Leertaste eines anlegen." },
	{ "No games yet. Press Y to add one.", "Noch keine Spiele. Mit Y eines anlegen." },
	{ "Return  play", "Return  Starten" },
	{ "Tab  details", "Tab  Details" },
	{ "Space  menu", "Leertaste  Menü" },
	{ "type to search", "Tippen zum Suchen" },
	{ "A  play", "A  Starten" },
	{ "X  details", "X  Details" },
	{ "Y  menu", "Y  Menü" },
	{ "B  back", "B  Zurück" },
	{ "L / R  jump", "L / R  Springen" },
	{ "A  play        B  back", "A  Starten        B  Zurück" },
	{ "Return  play        E  edit        Esc  back", "Return  Starten        E  Bearbeiten        Esc  Zurück" },
	{ "Search: %s", "Suche: %s" },
	{ "%s memory", "%s Speicher" },
	{ "Internet", "Internet" },
	{ "Sound card", "Soundkarte" },
	{ "Printer", "Drucker" },
	{ "%d disk", "%d Disk" },
	{ "%d disks", "%d Disks" },
	{ "Window %sx", "Fenster %sx" },
	{ "Played %ld time", "%ld-mal gespielt" },
	{ "Played %ld times", "%ld-mal gespielt" },

	/* starting EmuBuddy */
	{ "EmuBuddy was not found (set EMUBUDDY_EXE)", "EmuBuddy wurde nicht gefunden (EMUBUDDY_EXE setzen)" },
	{ "Could not start EmuBuddy: %s", "EmuBuddy ließ sich nicht starten: %s" },
	{ "Started %s", "%s gestartet" },
	{ "slot %d holds the built-in drives", "Slot %d gehört den eingebauten Laufwerken" },
	{ "the profile is too large", "Das Profil ist zu groß" },

	/* text fields */
	{ "Click in a text field first", "Zuerst in ein Textfeld klicken" },
	{ "Too long for this field", "Zu lang für dieses Feld" },
	{ "Pasted %d characters", "%d Zeichen eingesetzt" },
	{ "The clipboard has no text (copy some text first)", "Die Zwischenablage enthält keinen Text (zuerst Text kopieren)" },
	{ "Cut %d characters", "%d Zeichen ausgeschnitten" },
	{ "Copied %d characters", "%d Zeichen kopiert" },

	/* the editors */
	{ "New Game", "Neues Spiel" },
	{ "Edit Game", "Spiel bearbeiten" },
	{ "Edit Profile", "Profil bearbeiten" },
	{ "Name", "Name" },
	{ "Description", "Beschreibung" },
	{ "No picture", "Kein Bild" },
	{ "Choose Picture...", "Bild wählen..." },
	{ "Remove Picture", "Bild entfernen" },
	{ "or drop a picture", "oder ein Bild" },
	{ "or a disk here", "oder eine Disk hierher ziehen" },
	{ "Main disk", "Hauptdisk" },
	{ "Floppy disk", "Diskette" },
	{ "(none)", "(keine)" },
	{ "Library", "Bibliothek" },
	{ "Lib", "Bibl." },
	{ "Choose...", "Wählen..." },
	{ "Fast (16 MHz)", "Schnell (16 MHz)" },
	{ "Full screen", "Vollbild" },
	{ "Curved screen", "Gewölbter Schirm" },
	{ "Status bar", "Statuszeile" },
	{ "Screenshot", "Bildschirmfoto" },
	{ "Choose Screenshot...", "Bildschirmfoto wählen..." },
	{ "Games on This Screen", "Spiele auf diesem Bildschirm" },
	{ "Games on the Other Screen", "Spiele auf dem anderen Bildschirm" },
	{ "Only one screen is connected now: games open on it", "Nur ein Bildschirm ist angeschlossen: Spiele öffnen sich dort" },
	{ "Curved CRT look", "Gewölbter Röhrenschirm" },
	{ "More settings...", "Mehr Einstellungen..." },
	{ "Cancel", "Abbrechen" },
	{ "Save", "Sichern" },
	{ "Could not save %s", "%s ließ sich nicht sichern" },
	{ "Saved %s", "%s gesichert" },
	{ "%s copy", "%s Kopie" },
	{ "ROM file", "ROM-Datei" },
	{ "Hard disk 1", "Festplatte 1" },
	{ "Hard disk 2", "Festplatte 2" },
	{ "Hard disk 3", "Festplatte 3" },
	{ "Hard disk 4", "Festplatte 4" },
	{ "Hard disk 5", "Festplatte 5" },
	{ "Hard disk 6", "Festplatte 6" },
	{ "Hard disk 7", "Festplatte 7" },
	{ "CD-ROM", "CD-ROM" },
	{ "3.5\" drive 1", "3,5\"-Laufwerk 1" },
	{ "3.5\" drive 2", "3,5\"-Laufwerk 2" },
	{ "5.25\" drive 1", "5,25\"-Laufwerk 1" },
	{ "5.25\" drive 2", "5,25\"-Laufwerk 2" },
	{ "Shared folder", "Geteilter Ordner" },
	{ "Bridge folder", "Bridge-Ordner" },
	{ "Cover picture", "Titelbild" },
	{ "Memory", "Speicher" },
	{ "Speed (MHz)", "Tempo (MHz)" },
	{ "Speed", "Tempo" },
	{ "Slot 1 printer", "Slot 1 Drucker" },
	{ "Slot 2 modem", "Slot 2 Modem" },
	{ "Window size", "Fenstergröße" },
	{ "Extra options", "Weitere Optionen" },
	{ "Drop disks and folders here", "Disks und Ordner hierher ziehen" },

	/* the Profiles window of the Classic view */
	{ "Profiles", "Profile" },
	{ "New", "Neu" },
	{ "Delete", "Löschen" },
	{ "(missing)", "(fehlt)" },
	{ "Command line", "Befehlszeile" },
	{ "%d profile", "%d Profil" },
	{ "%d profiles", "%d Profile" },
	{ "Delete the profile \"%s\"? The disks it uses are not touched.",
		"Das Profil \"%s\" löschen? Die Disks, die es benutzt, bleiben unberührt." },
	{ "That size does not fit this screen", "Diese Größe passt nicht auf den Bildschirm" },

	/* the library */
	{ "ROMs", "ROMs" },
	{ "Disks", "Disks" },
	{ "Choose a ROM", "ROM wählen" },
	{ "Choose a disk", "Disk wählen" },
	{ "Search", "Suche" },
	{ "Check files", "Dateien prüfen" },
	{ "Checked %d files", "%d Dateien geprüft" },
	{ "(not used)", "(unbenutzt)" },
	{ "Nothing found in these folders.", "In diesen Ordnern wurde nichts gefunden." },
	{ "Add the folders that hold your ROMs and disks.", "Die Ordner mit deinen ROMs und Disks hinzufügen." },
	{ ", volume ", ", Volume " },
	{ ", write-protected", ", schreibgeschützt" },
	{ "Used by: %s", "Benutzt von: %s" },
	{ "no profile", "keinem Profil" },
	{ "Check: %s", "Prüfung: %s" },
	{ "Tags", "Stichwörter" },
	{ "Private Copy for Profile...", "Eigene Kopie für das Profil..." },
	{ "Library folders", "Ordner der Bibliothek" },
	{ "Add Folder...", "Ordner hinzufügen..." },
	{ "Remove Folder", "Ordner entfernen" },
	{ "Rescan", "Neu einlesen" },
	{ "Use", "Nehmen" },
	{ "Close", "Schließen" },
	{ "Library: %d files in 1 folder", "Bibliothek: %d Dateien in 1 Ordner" },
	{ "Library: %d files in %d folders", "Bibliothek: %d Dateien in %d Ordnern" },
	{ "No free drive in %s for this disk", "Kein freies Laufwerk in %s für diese Disk" },
	{ "Make Copy", "Kopie anlegen" },
	{ "Make a copy of \"%s\" (%ld MB) for the profile \"%s\" and use it as %s?\nThe original stays as it is.",
		"Eine Kopie von \"%s\" (%ld MB) für das Profil \"%s\" anlegen und als %s benutzen?\nDas Original bleibt, wie es ist." },
	{ "Copy failed: %s", "Kopieren fehlgeschlagen: %s" },
	{ "%s now uses its own copy as %s", "%s benutzt jetzt die eigene Kopie als %s" },
	{ "WOZ disk", "WOZ-Disk" },
	{ "Hard disk %ld MB", "Festplatte %ld MB" },
	{ "Disk %ld KB", "Disk %ld KB" },
	{ "same as %s", "gleich wie %s" },
	{ "size is not a whole number of blocks", "die Größe ist keine ganze Zahl von Blöcken" },
	{ "no ProDOS volume found", "kein ProDOS-Volume gefunden" },

	/* pictures */
	{ "Picture set for %s", "Bild für %s gesetzt" },
	{ "Could not copy the picture: %s", "Das Bild ließ sich nicht kopieren: %s" },
	{ "This game has no picture", "Dieses Spiel hat kein Bild" },
	{ "Delete the picture file of \"%s\"?\n\n%s", "Die Bilddatei von \"%s\" löschen?\n\n%s" },
	{ "Picture deleted", "Bild gelöscht" },
	{ "Could not delete: %s", "Löschen fehlgeschlagen: %s" },
	{ "Artwork folder: %s", "Bilderordner: %s" },

	/* import */
	{ "Import from Attract-Mode", "Aus Attract-Mode importieren" },
	{ "Import", "Importieren" },
	{ "Imported %d game%s", "%d Spiel importiert%s" },
	{ "Imported %d games%s", "%d Spiele importiert%s" },
	{ " (the others were there already)", " (die anderen gab es schon)" },
	{ "No Attract-Mode set-up found.", "Keine Attract-Mode-Einrichtung gefunden." },
	{ "Which system do you want to bring in?", "Welches System soll übernommen werden?" },
	{ "Your Attract-Mode folder is not changed. Games that exist already are skipped.",
		"Der Attract-Mode-Ordner bleibt, wie er ist. Vorhandene Spiele werden ausgelassen." },
	{ "Can't read the settings of %s", "Die Einstellungen von %s lassen sich nicht lesen" },
	{ "There is no game list for %s", "Für %s gibt es keine Spieleliste" },

	/* about */
	{ "Profiles and skins for EmuBuddy", "Profile und Skins für EmuBuddy" },
	{ "Starts EmuBuddy with the command line", "Startet EmuBuddy mit der Befehlszeile" },
	{ "of the profile you choose.", "des gewählten Profils." },
	{ "GPL v3. Skin fonts by Kreative Software,", "GPL v3. Skin-Schriften von Kreative Software," },
	{ "Wesley Clarke and Sizenko Alexander", "Wesley Clarke und Sizenko Alexander" },
	{ "(see fonts/CREDITS.md).", "(siehe fonts/CREDITS.md)." },
};
#define NTEXT	((int)(sizeof(g_text) / sizeof(g_text[0])))

static int	g_german, g_plain;

void
lang_set_german(int on)
{
	g_german = on != 0;
}

int
lang_german(void)
{
	return g_german;
}

void
lang_set_plain(int on)
{
	g_plain = on != 0;
}

int
lang_count(void)
{
	return NTEXT;
}

void
lang_entry(int i, const char **english, const char **german)
{
	*english = g_text[i].en;
	*german = g_text[i].de;
}

/* ä ö ü Ä Ö Ü ß written with plain letters, for a font that has no umlauts.
 * The result lives in one of a few buffers that are used in turn. */
static const char *
plain(const char *s)
{
	static char	buf[8][420];
	static int	turn;
	char		*out = buf[turn = (turn + 1) % 8];
	size_t		n = 0;

	for(; *s && n + 3 < sizeof(buf[0]); s++) {
		unsigned char c = (unsigned char)s[0], d = (unsigned char)s[1];
		const char *r = NULL;

		if(c == 0xC3) {
			r = d == 0xA4 ? "ae" : d == 0xB6 ? "oe" : d == 0xBC ? "ue" : d == 0x84 ? "Ae" :
				d == 0x96 ? "Oe" : d == 0x9C ? "Ue" : d == 0x9F ? "ss" : NULL;
		}
		if(r) {
			out[n++] = r[0];
			out[n++] = r[1];
			s++;
		} else {
			out[n++] = (char)c;
		}
	}
	out[n] = 0;
	return out;
}

const char *
lang_tr(const char *english)
{
	if(!g_german || !english) {
		return english;
	}
	for(int i = 0; i < NTEXT; i++) {
		if(!strcmp(g_text[i].en, english)) {
			return g_plain ? plain(g_text[i].de) : g_text[i].de;
		}
	}
	return english;
}
