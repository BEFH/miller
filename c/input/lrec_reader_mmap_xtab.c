// ================================================================
// Note: there are multiple process methods with a lot of code duplication.
// This is intentional. Much of Miller's measured processing time is in the
// lrec-reader process methods. This is code which needs to execute on every
// byte of input and even moving a single runtime if-statement into a
// function-pointer assignment at alloc time can have noticeable effects on
// performance (5-10% in some cases).
// ================================================================

#include <stdio.h>
#include <stdlib.h>
#include "cli/comment_handling.h"
#include "lib/mlr_globals.h"
#include "lib/mlrutil.h"
#include "input/file_reader_mmap.h"
#include "input/lrec_readers.h"

typedef struct _lrec_reader_mmap_xtab_state_t {
	char* ifs;
	char* ips;
	int   ifslen;
	int   ipslen;
	int   allow_repeat_ips;
	int   do_auto_line_term;
} lrec_reader_mmap_xtab_state_t;

static void    lrec_reader_mmap_xtab_free(lrec_reader_t* preader);
static void    lrec_reader_mmap_xtab_sof(void* pvstate, void* pvhandle);
static lrec_t* lrec_reader_mmap_xtab_process_single_ifs_single_ips(void* pvstate, void* pvhandle, context_t* pctx);
static lrec_t* lrec_reader_mmap_xtab_process_single_ifs_multi_ips(void* pvstate, void* pvhandle, context_t* pctx);
static lrec_t* lrec_reader_mmap_xtab_process_multi_ifs_single_ips(void* pvstate, void* pvhandle, context_t* pctx);
static lrec_t* lrec_reader_mmap_xtab_process_multi_ifs_multi_ips(void* pvstate, void* pvhandle, context_t* pctx);

static lrec_t* lrec_parse_mmap_xtab_single_ifs_single_ips(file_reader_mmap_state_t* phandle, char ifs, char ips,
	lrec_reader_mmap_xtab_state_t* pstate, context_t* pctx);

static lrec_t* lrec_parse_mmap_xtab_single_ifs_multi_ips(file_reader_mmap_state_t* phandle, char ifs,
	lrec_reader_mmap_xtab_state_t* pstate, context_t* pctx);

static lrec_t* lrec_parse_mmap_xtab_multi_ifs_single_ips(file_reader_mmap_state_t* phandle, char ips,
	lrec_reader_mmap_xtab_state_t* pstate);

static lrec_t* lrec_parse_mmap_xtab_multi_ifs_multi_ips(file_reader_mmap_state_t* phandle,
	lrec_reader_mmap_xtab_state_t* pstate);

// ----------------------------------------------------------------
lrec_reader_t* lrec_reader_mmap_xtab_alloc(char* ifs, char* ips, int allow_repeat_ips,
	comment_handling_t comment_handling, char* comment_string)
{
	// lrec_reader_alloc should have shunted away from us in this case.
	// (Interleaving blank-line handling, line-term autodetect, and comment-handling all in
	// the byte-at-a-time logic turned out to be a mess in this file. In the stdio implementation,
	// by constrast, it falls out rather easily.)
	if (comment_string != NULL) {
		fprintf(stderr, "%s: internal coding error detected in file %s at line %d.\n",
			MLR_GLOBALS.bargv0, __FILE__, __LINE__);
		exit(1);
	}

	lrec_reader_t* plrec_reader = mlr_malloc_or_die(sizeof(lrec_reader_t));

	lrec_reader_mmap_xtab_state_t* pstate = mlr_malloc_or_die(sizeof(lrec_reader_mmap_xtab_state_t));
	pstate->ifs                 = ifs;
	pstate->ips                 = ips;
	pstate->ifslen              = strlen(pstate->ifs);
	pstate->ipslen              = strlen(pstate->ips);
	pstate->allow_repeat_ips    = allow_repeat_ips;
	pstate->do_auto_line_term   = FALSE;

	plrec_reader->pvstate       = (void*)pstate;
	plrec_reader->popen_func    = file_reader_mmap_vopen;
	plrec_reader->pclose_func   = file_reader_mmap_vclose;

	if (streq(ifs, "auto")) {
		// Auto means either lines end in "\n" or "\r\n" (LF or CRLF).  In
		// either case the final character is "\n". Then for autodetect we
		// simply check if there's a character in the line before the '\n', and
		// if that is '\r'.
		pstate->do_auto_line_term = TRUE;
		pstate->ifs = "\n";
		pstate->ifslen = 1;
		plrec_reader->pprocess_func = (pstate->ipslen == 1)
			? lrec_reader_mmap_xtab_process_single_ifs_single_ips
			: lrec_reader_mmap_xtab_process_single_ifs_multi_ips;
	} else if (pstate->ifslen == 1) {
		plrec_reader->pprocess_func = (pstate->ipslen == 1)
			? lrec_reader_mmap_xtab_process_single_ifs_single_ips
			: lrec_reader_mmap_xtab_process_single_ifs_multi_ips;
	} else {
		plrec_reader->pprocess_func = (pstate->ipslen == 1)
			? lrec_reader_mmap_xtab_process_multi_ifs_single_ips
			: lrec_reader_mmap_xtab_process_multi_ifs_multi_ips;
	}

	plrec_reader->psof_func     = lrec_reader_mmap_xtab_sof;
	plrec_reader->pfree_func    = lrec_reader_mmap_xtab_free;

	return plrec_reader;
}

// ----------------------------------------------------------------
static void lrec_reader_mmap_xtab_free(lrec_reader_t* preader) {
	free(preader->pvstate);
	free(preader);
}

static void lrec_reader_mmap_xtab_sof(void* pvstate, void* pvhandle) {
}

// ----------------------------------------------------------------
static lrec_t* lrec_reader_mmap_xtab_process_single_ifs_single_ips(void* pvstate, void* pvhandle, context_t* pctx) {
	file_reader_mmap_state_t* phandle = pvhandle;
	lrec_reader_mmap_xtab_state_t* pstate = pvstate;
	if (phandle->sol >= phandle->eof)
		return NULL;
	else
		return lrec_parse_mmap_xtab_single_ifs_single_ips(phandle, pstate->ifs[0], pstate->ips[0],
			pstate, pctx);
}

static lrec_t* lrec_reader_mmap_xtab_process_single_ifs_multi_ips(void* pvstate, void* pvhandle, context_t* pctx) {
	file_reader_mmap_state_t* phandle = pvhandle;
	lrec_reader_mmap_xtab_state_t* pstate = pvstate;
	if (phandle->sol >= phandle->eof)
		return NULL;
	else
		return lrec_parse_mmap_xtab_single_ifs_multi_ips(phandle, pstate->ifs[0], pstate, pctx);
}

static lrec_t* lrec_reader_mmap_xtab_process_multi_ifs_single_ips(void* pvstate, void* pvhandle, context_t* pctx) {
	file_reader_mmap_state_t* phandle = pvhandle;
	lrec_reader_mmap_xtab_state_t* pstate = pvstate;
	if (phandle->sol >= phandle->eof)
		return NULL;
	else
		return lrec_parse_mmap_xtab_multi_ifs_single_ips(phandle, pstate->ips[0], pstate);
}

static lrec_t* lrec_reader_mmap_xtab_process_multi_ifs_multi_ips(void* pvstate, void* pvhandle, context_t* pctx) {
	file_reader_mmap_state_t* phandle = pvhandle;
	lrec_reader_mmap_xtab_state_t* pstate = pvstate;
	if (phandle->sol >= phandle->eof)
		return NULL;
	else
		return lrec_parse_mmap_xtab_multi_ifs_multi_ips(phandle, pstate);
}

// ----------------------------------------------------------------
static lrec_t* lrec_parse_mmap_xtab_single_ifs_single_ips(file_reader_mmap_state_t* phandle, char ifs, char ips,
	lrec_reader_mmap_xtab_state_t* pstate, context_t* pctx)
{
	if (pstate->do_auto_line_term) {
		// Skip over otherwise empty LF-only or CRLF-only lines.
		while (phandle->sol < phandle->eof) {
			if (*phandle->sol == '\n') {
				context_set_autodetected_lf(pctx);
				phandle->sol += 1;
			} else if (*phandle->sol == '\r') {
				char* q = phandle->sol + 1;
				if (q < phandle->eof && *q == '\n') {
					context_set_autodetected_crlf(pctx);
					phandle->sol += 2;
				} else {
					phandle->sol += 1;
				}
			} else {
				break;
			}
		}
	} else {
		// Skip over otherwise empty IFS-only lines
		while (phandle->sol < phandle->eof && *phandle->sol == ifs) {
			phandle->sol++;
		}
	}

	if (phandle->sol >= phandle->eof)
		return NULL;

	lrec_t* prec = lrec_unbacked_alloc();

	// Loop over fields, one per line
	while (TRUE) {
		if (phandle->sol >= phandle->eof)
			break;

		char* line  = phandle->sol;
		char* key   = line;
		char* value = "";
		char* p;
		int saw_ips_in_field = FALSE;

		// Construct one field
		int saw_eol = FALSE;
		for (p = line; p < phandle->eof && *p; ) {
			if (*p == ifs) {
				saw_ips_in_field = FALSE;
				*p = 0;

				if (pstate->do_auto_line_term) {
					if (p > line && p[-1] == '\r') {
						p[-1] = 0;
						context_set_autodetected_crlf(pctx);
					} else {
						context_set_autodetected_lf(pctx);
					}
				}

				phandle->sol = p+1;
				saw_eol = TRUE;
				break;
			} else if (!saw_ips_in_field && *p == ips) {
				saw_ips_in_field = TRUE;
				key = line;
				*p = 0;

				p++;
				if (pstate->allow_repeat_ips) {
					while (*p == ips)
						p++;
				}
				value = p;
			} else {
				p++;
			}
		}
		if (p >= phandle->eof)
			phandle->sol = p+1;

		if (saw_eol) {
			// Easy and simple case: we read until end of line.  We zero-poked the irs to a null character to terminate
			// the C string so it's OK to retain a pointer to that.
			lrec_put(prec, key, value, NO_FREE);
		} else {
			// Messier case: we read to end of file without seeing end of line.  We can't always zero-poke a null
			// character to terminate the C string: if the file size is not a multiple of the OS page size it'll work
			// (it's our copy-on-write memory). But if the file size is a multiple of the page size, then zero-poking at
			// EOF is one byte past the page and that will segv us.
			char* copy = mlr_alloc_string_from_char_range(value, phandle->eof - value);
			lrec_put(prec, key, copy, FREE_ENTRY_VALUE);
		}

		if (phandle->sol >= phandle->eof)
			break;

		if (pstate->do_auto_line_term) {
			char* p = phandle->sol;
			char* q = phandle->sol + 1;
			if (*p == '\n')
				break;
			if (q < phandle->eof && *p == '\r' && *q == '\n')
				break;
		} else {
			if (*phandle->sol == ifs)
				break;
		}
	}
	if (prec->field_count == 0) {
		lrec_free(prec);
		return NULL;
	} else {
		return prec;
	}
}

static lrec_t* lrec_parse_mmap_xtab_single_ifs_multi_ips(file_reader_mmap_state_t* phandle, char ifs,
	lrec_reader_mmap_xtab_state_t* pstate, context_t* pctx)
{
	if (pstate->do_auto_line_term) {
		// Skip over otherwise empty LF-only or CRLF-only lines.
		while (phandle->sol < phandle->eof) {
			if (*phandle->sol == '\n') {
				context_set_autodetected_lf(pctx);
				phandle->sol += 1;
			} else if (*phandle->sol == '\r') {
				char* q = phandle->sol + 1;
				if (q < phandle->eof && *q == '\n') {
					context_set_autodetected_crlf(pctx);
					phandle->sol += 2;
				} else {
					phandle->sol += 1;
				}
			} else {
				break;
			}
		}
	} else {
		// Skip over otherwise empty IFS-only lines.
		while (phandle->sol < phandle->eof && *phandle->sol == ifs)
			phandle->sol++;
	}

	if (phandle->sol >= phandle->eof)
		return NULL;

	char* ips = pstate->ips;
	int ipslen = pstate->ipslen;

	lrec_t* prec = lrec_unbacked_alloc();

	// Loop over fields, one per line
	while (TRUE) {
		if (phandle->sol >= phandle->eof)
			break;

		char* line  = phandle->sol;
		char* key   = line;
		char* value = "";
		char* p;
		int saw_ips_in_field = FALSE;

		// Construct one field
		int saw_eol = FALSE;
		for (p = line; p < phandle->eof && *p; ) {
			if (*p == ifs) {
				saw_ips_in_field = FALSE;
				*p = 0;

				if (pstate->do_auto_line_term) {
					if (p > line && p[-1] == '\r') {
						p[-1] = 0;
						context_set_autodetected_crlf(pctx);
					} else {
						context_set_autodetected_lf(pctx);
					}
				}

				phandle->sol = p+1;
				saw_eol = TRUE;
				break;
			} else if (!saw_ips_in_field && streqn(p, ips, ipslen)) {
				saw_ips_in_field = TRUE;
				key = line;
				*p = 0;

				p += ipslen;
				if (pstate->allow_repeat_ips) {
					while (streqn(p, ips, ipslen))
						p += ipslen;
				}
				value = p;
			} else {
				p++;
			}
		}
		if (p >= phandle->eof)
			phandle->sol = p+1;

		if (saw_eol) {
			// Easy and simple case: we read until end of line.  We zero-poked the irs to a null character to terminate
			// the C string so it's OK to retain a pointer to that.
			lrec_put(prec, key, value, NO_FREE);
		} else {
			// Messier case: we read to end of file without seeing end of line.  We can't always zero-poke a null
			// character to terminate the C string: if the file size is not a multiple of the OS page size it'll work
			// (it's our copy-on-write memory). But if the file size is a multiple of the page size, then zero-poking at
			// EOF is one byte past the page and that will segv us.
			char* copy = mlr_alloc_string_from_char_range(value, phandle->eof - value);
			lrec_put(prec, key, copy, FREE_ENTRY_VALUE);
		}

		if (phandle->sol >= phandle->eof || *phandle->sol == ifs)
			break;
	}
	if (prec->field_count == 0) {
		lrec_free(prec);
		return NULL;
	} else {
		return prec;
	}
}

static lrec_t* lrec_parse_mmap_xtab_multi_ifs_single_ips(file_reader_mmap_state_t* phandle, char ips,
	lrec_reader_mmap_xtab_state_t* pstate)
{
	char* ifs = pstate->ifs;
	int ifslen = pstate->ifslen;

	// Skip blank lines
	while (phandle->eof - phandle->sol >= ifslen && streqn(phandle->sol, ifs, ifslen)) {
		phandle->sol += ifslen;
	}

	if (phandle->sol >= phandle->eof)
		return NULL;

	lrec_t* prec = lrec_unbacked_alloc();

	// Loop over fields, one per line
	while (TRUE) {
		if (phandle->sol >= phandle->eof)
			break;

		char* line  = phandle->sol;
		char* key   = line;
		char* value = "";
		char* p;
		int saw_ips_in_field = FALSE;

		// Construct one field
		int saw_eol = FALSE;
		for (p = line; p < phandle->eof && *p; ) {
			if (streqn(p, ifs, ifslen)) {
				saw_ips_in_field = FALSE;
				*p = 0;
				phandle->sol = p + ifslen;
				saw_eol = TRUE;
				break;
			} else if (!saw_ips_in_field && *p == ips) {
				saw_ips_in_field = TRUE;
				key = line;
				*p = 0;

				p++;
				if (pstate->allow_repeat_ips) {
					while (*p == ips)
						p++;
				}
				value = p;
			} else {
				p++;
			}
		}
		if (p >= phandle->eof)
			phandle->sol = p+1;

		if (saw_eol) {
			// Easy and simple case: we read until end of line.  We zero-poked the irs to a null character to terminate
			// the C string so it's OK to retain a pointer to that.
			lrec_put(prec, key, value, NO_FREE);
		} else {
			// Messier case: we read to end of file without seeing end of line.  We can't always zero-poke a null
			// character to terminate the C string: if the file size is not a multiple of the OS page size it'll work
			// (it's our copy-on-write memory). But if the file size is a multiple of the page size, then zero-poking at
			// EOF is one byte past the page and that will segv us.
			char* copy = mlr_alloc_string_from_char_range(value, phandle->eof - value);
			lrec_put(prec, key, copy, FREE_ENTRY_VALUE);
		}

		if (phandle->sol >= phandle->eof || streqn(phandle->sol, ifs, ifslen))
			break;
	}
	if (prec->field_count == 0) {
		lrec_free(prec);
		return NULL;
	} else {
		return prec;
	}
}

static lrec_t* lrec_parse_mmap_xtab_multi_ifs_multi_ips(file_reader_mmap_state_t* phandle,
	lrec_reader_mmap_xtab_state_t* pstate)
{
	char* ips = pstate->ips;
	int ipslen = pstate->ipslen;
	char* ifs = pstate->ifs;
	int ifslen = pstate->ifslen;

	// Skip blank lines
	while (phandle->eof - phandle->sol >= ifslen && streqn(phandle->sol, ifs, ifslen)) {
		phandle->sol += ifslen;
	}

	if (phandle->sol >= phandle->eof)
		return NULL;

	lrec_t* prec = lrec_unbacked_alloc();

	// Loop over fields, one per line
	while (TRUE) {
		if (phandle->sol >= phandle->eof)
			break;

		char* line  = phandle->sol;
		char* key   = line;
		char* value = "";
		char* p;
		int saw_ips_in_field = FALSE;

		// Construct one field
		int saw_eol = FALSE;
		for (p = line; p < phandle->eof && *p; ) {
			if (streqn(p, ifs, ifslen)) {
				saw_ips_in_field = FALSE;
				*p = 0;
				phandle->sol = p + ifslen;
				saw_eol = TRUE;
				break;
			} else if (!saw_ips_in_field && streqn(p, ips, ipslen)) {
				saw_ips_in_field = TRUE;
				key = line;
				*p = 0;

				p += ipslen;
				if (pstate->allow_repeat_ips) {
					while (streqn(p, ips, ipslen))
						p += ipslen;
				}
				value = p;
			} else {
				p++;
			}
		}
		if (p >= phandle->eof)
			phandle->sol = p+1;

		if (saw_eol) {
			// Easy and simple case: we read until end of line.  We zero-poked the irs to a null character to terminate
			// the C string so it's OK to retain a pointer to that.
			lrec_put(prec, key, value, NO_FREE);
		} else {
			// Messier case: we read to end of file without seeing end of line.  We can't always zero-poke a null
			// character to terminate the C string: if the file size is not a multiple of the OS page size it'll work
			// (it's our copy-on-write memory). But if the file size is a multiple of the page size, then zero-poking at
			// EOF is one byte past the page and that will segv us.
			char* copy = mlr_alloc_string_from_char_range(value, phandle->eof - value);
			lrec_put(prec, key, copy, FREE_ENTRY_VALUE);
		}

		if (phandle->sol >= phandle->eof || streqn(phandle->sol, ifs, ifslen))
			break;
	}
	if (prec->field_count == 0) {
		lrec_free(prec);
		return NULL;
	} else {
		return prec;
	}
}
