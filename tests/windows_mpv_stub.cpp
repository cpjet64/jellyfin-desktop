#ifndef MPV_STUB_VALUE
#error MPV_STUB_VALUE must identify the fixture.
#endif
extern "C" __declspec(dllexport) int mpv_fixture()
{
  return MPV_STUB_VALUE;
}
