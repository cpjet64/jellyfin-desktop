int qualification_a(int value);
int qualification_b(int value);

int main(void)
{
    return qualification_a(1) + qualification_b(1) == 30 ? 0 : 1;
}
