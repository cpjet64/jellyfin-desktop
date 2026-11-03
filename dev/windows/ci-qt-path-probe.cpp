#include <QCoreApplication>
#include <QStandardPaths>
#include <QTextStream>

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  const struct { const char* name; QStandardPaths::StandardLocation location; } paths[] = {
    {"ConfigLocation", QStandardPaths::ConfigLocation},
    {"GenericConfigLocation", QStandardPaths::GenericConfigLocation},
    {"CacheLocation", QStandardPaths::CacheLocation},
    {"GenericCacheLocation", QStandardPaths::GenericCacheLocation},
  };
  for (const auto& entry : paths)
    out << entry.name << '=' << QStandardPaths::writableLocation(entry.location) << Qt::endl;
  return 0;
}
