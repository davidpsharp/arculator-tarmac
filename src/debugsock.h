/*
  Arculator - Acorn Archimedes emulator

  DebugSock - expose the built-in debugger over a local TCP socket.

  A thin front end for the existing debugger console: everything debug_out()
  prints is mirrored to a connected client, and a line sent by the client is
  taken as a debugger command in place of one typed in the console window.
  The debugger itself is unchanged, so both front ends offer the same
  commands and the wx console keeps working when no client is connected.

  A client can also ask for a pause while the machine is running ("pause"),
  which is the one thing the console cannot do.

  Everything here runs on the emulator thread, like HostCmd, so no locking
  is involved.
 */

#ifndef DEBUGSOCK_H
#define DEBUGSOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle, called from arc_init()/arc_run()/arc_close() in main.c. */
void debugsock_init(void);
void debugsock_close(void);
void debugsock_poll(void);	/**< Accept/read; handles out-of-band commands. */

/* Used by debugger.c. */
int  debugsock_connected(void);		/**< A client is attached. */
void debugsock_output(const char *s);	/**< Mirror debugger output to it. */
int  debugsock_input_get(char *s);	/**< Next queued command line, or 0. */

/* Configuration (machine .cfg keys debugsock_enabled, debugsock_port).
   After debugsock_init(), debugsock_port holds the port actually bound. */
extern int debugsock_enabled;
extern int debugsock_port;

#ifdef __cplusplus
}
#endif

#endif /* DEBUGSOCK_H */
