/* setuptools builds the C library as a "Python extension" so `pip install`
 * works everywhere; it is actually loaded with ctypes. This stub satisfies the
 * linker's requirement for an init symbol (needed on Windows). */
#if defined(_WIN32)
__declspec(dllexport)
#endif
void *PyInit__libtdbg(void) { return 0; }
