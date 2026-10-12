// Run the established matrix with B selected, retaining a real main function.
#define TIME_RESEARCH_CONCRETE_HOST_TEST
#ifdef TIME_RESEARCH_CONCRETE_BOUNDARIES
#include "smoke_research_machine_boundaries.cpp"
#else
#include "smoke_research_machine_block.cpp"
#endif
