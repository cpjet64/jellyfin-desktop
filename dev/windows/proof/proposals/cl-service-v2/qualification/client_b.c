/* Qualification-only object: non-inlined symbols for the PDB consumer. */
__declspec(noinline) int qualification_b(int value)
{
    volatile int result = value + 17;
    return result;
}
