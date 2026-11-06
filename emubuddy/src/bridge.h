/* EmuBuddy host bridge: lets programs inside the IIgs read and write files in
 * ONE folder of the host computer (the "bridge folder", chosen by the user).
 * The program calls it with the CPU instruction WDM $EB,$00 (a no-op on real
 * hardware); see docs/BRIDGE.md for the protocol. */
#ifndef EMUBUDDY_BRIDGE_H
#define EMUBUDDY_BRIDGE_H

#define BRIDGE_VERSION		1
#define BRIDGE_MAX_XFER		32768

/* How the bridge reads and writes IIgs memory (24-bit addresses). The
 * emulator sets these to its own memory access; the host test sets a flat
 * array. */
typedef unsigned (*BridgeRd)(unsigned long addr);
typedef void (*BridgeWr)(unsigned long addr, unsigned val);
void	bridge_set_memory(BridgeRd rd, BridgeWr wr);

/* The bridge folder, as a host path ("" or NULL = bridge is off). Normally
 * the config variable g_bridge_root; tests set it directly. */
extern char	*g_bridge_root;

/* Optional: returns 1 if the folder is also mounted as a disk volume. The
 * bridge then refuses to work, so the two can't write to the same files. */
extern int	(*bridge_overlap_check)(const char *folder);

/* Run the command in the parameter block at addr. The status word in the
 * block (offset 2) is set; the same value is returned (0 = ok). */
int	bridge_command(unsigned long block);

/* Close every open file (reset, quit). */
void	bridge_close_all(void);

/* Host name <-> ProDOS name and type (shared-folder convention). Exposed for
 * tests. */
void	bridge_decode_name(const char *host, char *out, int outsize, int *ftype,
								int *aux);
void	bridge_encode_name(const char *prodos, int ftype, int aux, char *out,
								int outsize);

/* ---- Dropping files into a GS/OS folder that the shared folder can't reach ----
 * The Mac side stages the file in <bridge folder>/Inbox/NNNN/ together with a
 * target.txt that holds the GS/OS path of the folder; the program "Receive"
 * (tools/receive) running in the IIgs copies it there and leaves a note in
 * <bridge folder>/Outbox/. */

/* ProDOS-safe name (15 characters, a letter first, letters digits and dots)
 * and the type and aux type chosen from the extension. */
void	bridge_prodos_name(const char *host, char *out, int outsize, int *ftype,
								int *aux);

/* Stage src for the GS/OS folder (":Volume:Folder"). Returns 0, or -1 with
 * the reason in msg. */
int	bridge_stage_file(const char *src, const char *gsos_folder, char *msg,
								int msgsize);

/* The next note the IIgs left in Outbox ("OK ..." or "ERR ..."); removes it.
 * Returns 1 if there was one. */
int	bridge_next_result(char *msg, int msgsize);

#endif
