#include "test_healthy_graph_dispatch_fixture.h"

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  TestHealthyGraphDispatch::runLoadCases();
  TestHealthyGraphDispatch::runSeekCases();
}
