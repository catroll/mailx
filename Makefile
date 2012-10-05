#@ Makefile for S-nail.
#@ See the file INSTALL if you need help.

# General prefix
PREFIX		= /usr/local

BINDIR		= $(PREFIX)/bin
MANDIR		= $(PREFIX)/man
SYSCONFDIR	= $(PREFIX)/etc

# Prepended to all paths at installation time (for e.g. package building)
DESTDIR		=
# (For those who want to install S-nail(1) as nail(1), use an empty *SID*)
SID		= s-

MAILSPOOL	= /var/mail
SENDMAIL	= /usr/sbin/sendmail
SHELL		= /bin/sh
STRIP		= strip
INSTALL		= /usr/bin/install

#CFLAGS		=
#WARN		= -W -Wall -pedantic
#LDFLAGS		=

##  --  >8  --  8<  --  ##

# To ease the life of forkers and packagers one may even adjust the "nail"
# of nail(1).  Note that $(SID)$(NAIL) must be longer than two characters.
# There you go.  Two lines for a completely clean fork.
NAIL		= nail
SYSCONFRC	= $(SYSCONFDIR)/$(SID)$(NAIL).rc

# Binaries builtin paths
PATHDEFS	= -DSYSCONFRC='"$(SYSCONFRC)"' -DMAILSPOOL='"$(MAILSPOOL)"' \
			-DSENDMAIL='"$(SENDMAIL)"'

OBJ = aux.o base64.o cache.o cmd1.o cmd2.o cmd3.o cmdtab.o collect.o \
	dotlock.o edit.o fio.o getname.o getopt.o head.o hmac.o \
	imap.o imap_search.o junk.o lex.o list.o lzw.o \
	macro.o maildir.o main.o md5.o mime.o names.o nss.o \
	openssl.o pop3.o popen.o quit.o \
	send.o sendout.o smtp.o ssl.o strings.o temp.o thread.o tty.o \
	v7.local.o vars.o \
	version.o

.SUFFIXES: .o .c .x .y
.c.o:
	$(CC) $(CFLAGS) $(WARN) $(FEATURES) `cat INCS` -c $<

.c.x:
	$(CC) $(CFLAGS) $(WARN) $(FEATURES) -E $< >$@

.c .y: ;

all: $(SID)$(NAIL)

$(SID)$(NAIL): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) `cat LIBS` -o $@

$(OBJ): config.h def.h extern.h glob.h rcv.h
imap.o: imap_gssapi.c
md5.o imap.o hmac.o smtp.o aux.o pop3.o junk.o: md5.h
nss.o: nsserr.c

new-version:
	[ -z "$${VERSION}" ] && eval VERSION="`git describe --dirty --tags`"; \
	echo > version.c \
	"const char *const uagent = \"$(SID)$(NAIL)\", \
	*const version = \"$${VERSION:-spooky}\";"

config.h: user.conf makeconfig Makefile
	$(SHELL) ./makeconfig

mkman.1: nail.1
	_SYSCONFRC="$(SYSCONFRC)" _NAIL="$(SID)$(NAIL)" \
	< $< > $@ awk 'BEGIN {written = 0} \
	/.\"--MKMAN-START--/, /.\"--MKMAN-END--/ { \
		if (written == 1) \
			next; \
		written = 1; \
		OFS = ""; \
		unail = toupper(ENVIRON["_NAIL"]); \
		lnail = tolower(unail); \
		cnail = toupper(substr(lnail, 1, 1)) substr(lnail, 2); \
		print ".ds UU ", unail; \
		print ".ds uu ", cnail; \
		print ".ds UA \\\\fI", cnail, "\\\\fR"; \
		print ".ds ua \\\\fI", lnail, "\\\\fR"; \
		print ".ds ba \\\\fB", lnail, "\\\\fR"; \
		print ".ds UR ", ENVIRON["_SYSCONFRC"]; \
		OFS = " "; \
		next \
	} \
	{print} \
	'

install: all mkman.1
	test -d $(DESTDIR)$(BINDIR) || mkdir -p $(DESTDIR)$(BINDIR)
	$(INSTALL) -c $(SID)$(NAIL) $(DESTDIR)$(BINDIR)/$(SID)$(NAIL)
	$(STRIP) $(DESTDIR)$(BINDIR)/$(SID)$(NAIL)
	test -d $(DESTDIR)$(MANDIR)/man1 || mkdir -p $(DESTDIR)$(MANDIR)/man1
	$(INSTALL) -c -m 644 mkman.1 $(DESTDIR)$(MANDIR)/man1/$(SID)$(NAIL).1
	test -d $(DESTDIR)$(SYSCONFDIR) || mkdir -p $(DESTDIR)$(SYSCONFDIR)
	test -f $(DESTDIR)$(MAILRC) || \
		$(INSTALL) -c -m 644 nail.rc $(DESTDIR)$(MAILRC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(SID)$(NAIL) \
		$(DESTDIR)$(MANDIR)/man1/$(SID)$(NAIL).1

clean:
	rm -f $(OBJ) $(SID)$(NAIL) mkman.1 *~ core log

distclean: clean
	rm -f config.h config.log LIBS INCS

