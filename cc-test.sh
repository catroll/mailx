#!/bin/sh -
#@ XXX Add tests

# NOTE!  UnixWare 7.1.4 gives ISO-10646-Minimum-European-Subset for
# nl_langinfo(CODESET), then, so also overwrite ttycharset.
# (In addition this setup allows us to succeed on TinyCore 4.4 that has no
# other locales than C/POSIX installed by default!)
LC=en_US.UTF-8
LC_ALL=${LC} LANG=${LC}
ttycharset=UTF-8
export LC_ALL LANG ttycharset

MAKE=make
NAIL=./s-nail

OUT=./.cc-test.out
ERR=./.cc-test.err
BODY=./.cc-body.txt
MBOX1=./.cc-t1.mbox
MBOX2=./.cc-t2.mbox
ESTAT=0

rm -f "${OUT}" "${ERR}" "${BODY}" "${MBOX1}" "${MBOX2}" 2>> "${ERR}"

# Test all configs
cc_all_configs() {
	for c in MINIMAL NETLESS NETSEND CUSTOM \
			'CUSTOM WANT_ASSERTS=1' \
			'CUSTOM WANT_ASSERTS=1 WANT_NOALLOCA=1' \
			'CUSTOM WANT_ASSERTS=1 WANT_NOALLOCA=1 WANT_NOGETOPT=1'
	do
		printf "\n\n##########\nCONFIG=$c\n"
		printf "\n\n##########\nCONFIG=$c\n" >&2
		sh -c "${MAKE} CONFIG=${c}"
		${MAKE} distclean
	done >> "${OUT}" 2>> "${ERR}"
}

# Test a UTF-8 mail as a whole via -t, and in pieces (without -t ;)
cksum_test() {
	f=$1 s=$2
	[ "`sed -e 1,2d -e '/ boundary=/d' -e /--=_/d < \"${f}\" | cksum`" != \
			"${s}" ] && {
		ESTAT=1
		echo "Checksum mismatch test: ${f}" 2>> "${ERR}"
	}
}

test_mail() {
	printf "\n\n########################################\n\n" >> "${OUT}"
	printf "\n\n########################################\n\n" >> "${ERR}"
	"${MAKE}" >> "${OUT}" 2>> "${ERR}"

	< "${BODY}" MAILRC=/dev/null \
	"${NAIL}" -n -Sstealthmua -a "${BODY}" -s "${SUB}" "${MBOX1}"
	(	echo "To: ${MBOX2}" && echo "Subject: ${SUB}" && echo &&
		cat "${BODY}"
	) | MAILRC=/dev/null "${NAIL}" -n -Sstealthmua -a "${BODY}" -t

	cksum_test "${MBOX1}" '3520698923 4771'
	cksum_test "${MBOX2}" '1870415345 4770'
}

printf \
'Ich bin eine DÖS-Datäi mit sehr langen Zeilen und auch '\
'sonst bin ich ganz schön am Schleudern, da kannste denke '\
"wasde willst, gelle, gelle, gelle, gelle, gelle.\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst \r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 1\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 12\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 123\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 1234\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 12345\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 123456\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 1234567\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 12345678\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 123456789\r\n"\
"bin\r\n"\
"eine\r\n"\
"DOS-Datei\r\n"\
"ohne\r\n"\
"vertikalem\r\n"\
"Tabulator.\r\n"\
"Unn ausserdem habe ich trailing SP/HT/SP/HT whitespace 	 	\r\n"\
"Unn ausserdem habe ich trailing HT/SP/HT/SP whitespace	 	 \r\n"\
"auf den zeilen vorher.\r\n"\
"From am Zeilenbeginn und From der Mitte gibt es auch.\r\n"\
".\r\n"\
"Die letzte Zeile war nur ein Punkt.\r\n"\
"..\r\n"\
"Das waren deren zwei.\r\n"\
" \r\n"\
"Die letzte Zeile war ein Leerschritt.\n"\
"=VIER = EQUAL SIGNS=ON A LINE=\r\n"\
"Prösterchen.\r\n"\
".\n"\
"Die letzte Zeile war nur ein Punkt, mit Unix Zeilenende.\n"\
"..\n"\
"Das waren deren zwei.  ditto.\n"\
"Prösterchen.\n"\
"Unn ausseerdem habe ich trailing SP/HT/SP/HT whitespace 	 	\n"\
"Unn ausseerdem habe ich trailing HT/SP/HT/SP whitespace	 	 \n"\
"auf den zeilen vorher.\n"\
"ditto.\n"\
"=VIER = EQUAL SIGNS=ON A LINE=\n"\
" \n"\
"Die letzte Zeile war ein Leerschritt.\n"\
' '\
	> "${BODY}"

SUB='Äbrä  Kä?dä=brö 	 Fü?di=bus? '\
'adadaddsssssssddddddddddddddddddddd'\
'ddddddddddddddddddddddddddddddddddd'\
'ddddddddddddddddddddddddddddddddddd'\
'dddddddddddddddddddd Hallelulja? Od'\
'er?? eeeeeeeeeeeeeeeeeeeeeeeeeeeeee'\
'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'\
'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee f'\
'fffffffffffffffffffffffffffffffffff'\
'fffffffffffffffffffff ggggggggggggg'\
'ggggggggggggggggggggggggggggggggggg'\
'ggggggggggggggggggggggggggggggggggg'\
'ggggggggggggggggggggggggggggggggggg'\
'gggggggggggggggg'

cc_all_configs
test_mail

if [ ${ESTAT} -eq 0 ]; then
	"${MAKE}" distclean >> "${OUT}" 2>> "${ERR}"
	rm -f "${BODY}" "${MBOX1}" "${MBOX2}" >> "${OUT}" 2>> "${ERR}"
fi
exit ${ESTAT}
# vim:set fenc=utf8 syntax=sh ts=8 sts=8 sw=8 noet tw=79:
