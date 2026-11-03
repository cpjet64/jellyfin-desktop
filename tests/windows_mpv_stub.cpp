#ifndef MPV_STUB_VALUE
#error MPV_STUB_VALUE must identify the fixture.
#endif
#ifdef MPV_STUB_DEPENDENCY
extern "C" __declspec(dllimport) int fixture_dependency();
#endif
#ifdef MPV_STUB_MISSING_EXPORT
extern "C" __declspec(dllexport) int other_fixture()
#else
extern "C" __declspec(dllexport) int mpv_fixture()
#endif
{
#ifdef MPV_STUB_DEPENDENCY
  return fixture_dependency();
#else
  return MPV_STUB_VALUE;
#endif
}
