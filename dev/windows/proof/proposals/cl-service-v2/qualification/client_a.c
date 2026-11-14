/* Qualification-only object: non-inlined symbols for the PDB consumer. */
__declspec(noinline) int qualification_a(int value)
{
    volatile int result = value + 11;
    return result;
}
