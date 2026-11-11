/*
 * dlprobe.c - native 32-bit dlopen/dlsym probe for the StargateTC export harness.
 *
 * The GoldSrc server plugins (ref/stc_i386.so and the rebuilt hl.so) are ELF
 * 32-bit i386 objects.  A 64-bit Python interpreter cannot ctypes-load them
 * ("wrong ELF class"), so the harness compiles this tiny helper with -m32 and
 * runs it to prove that (a) the shared object's dependencies resolve and
 * (b) each expected factory/export actually resolves through the dynamic
 * linker rather than merely appearing in a static symbol table.
 *
 * RTLD_NOW is tried first.  A plugin that links with an unresolved function
 * (the linker permits undefined symbols in a shared object by default) fails
 * RTLD_NOW but still loads under RTLD_LAZY, so the helper retries lazily and
 * reports which mode succeeded.  Either way every requested name is dlsym'd,
 * which separates "symbol absent from the binary" from "binary has an
 * unresolved dependency".
 *
 * Usage:  dlprobe <path/to/lib.so> <file-with-one-symbol-name-per-line>
 * Output: tab-separated records, one per line:
 *           DLOPEN <OK|FAIL> <NOW|LAZY|-> <detail>
 *           SYM    <OK|MISSING> <name>
 * Exit:   0 = ran to completion, 2 = dlopen failed in both modes,
 *         3 = symbol file unreadable.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	void *h;
	const char *mode;
	FILE *f;
	char line[1024];

	if (argc < 3) {
		fprintf(stderr, "usage: %s <lib.so> <symfile>\n", argv[0]);
		return 64;
	}

	h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
	if (h) {
		mode = "NOW";
	} else {
		/* Retry lazily so per-symbol resolution can still be reported. */
		h = dlopen(argv[1], RTLD_LAZY | RTLD_GLOBAL);
		mode = h ? "LAZY" : "-";
	}

	if (!h)
		printf("DLOPEN\tFAIL\t-\t%s\n", dlerror());
	else
		printf("DLOPEN\tOK\t%s\t%s\n", mode, argv[1]);

	if (!h)
		return 2; /* no handle: per-symbol results would all be false */

	f = fopen(argv[2], "r");
	if (!f) {
		perror("fopen");
		return 3;
	}
	while (fgets(line, sizeof line, f)) {
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (!n)
			continue;
		printf("SYM\t%s\t%s\n",
		       (h && dlsym(h, line)) ? "OK" : "MISSING", line);
	}
	fclose(f);
	return h ? 0 : 2;
}
