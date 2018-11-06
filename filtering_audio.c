/* Main routing for standalone filtering_audio program */

#include "spettro.h"
#include "libav.h"

int
main(int argc, char **argv)
{
#if USE_LIBAV
    return filtering_main(argc, argv);
#else
    fprintf(stderr, "%s wasn't built with USE_LIBAV\n", argv[0]);
    return 1;
#endif
}
