#include "c2_fault_restore_permission.h"
#include <cstdio>

using Permission = C2FaultRestorePermission;

int main()
{
  const Permission::Identity identity{1, 2, 3, 4, 5, "owned clip", 2.0, true};
  const Permission::Proof proof{identity, 6, true, true, true, true, true, true};
  int failures = 0;
  const auto check = [&](bool value) { if (!value) ++failures; };
  { Permission p(identity, 100); check(p.consume(proof, 50)); check(!p.consume(proof, 51));
    check(!p.mayContinue(identity, 6, 52)); p.reloadResult(true);
    check(p.mayContinue(identity, 6, 52)); check(!p.mayContinue(identity, 4, 52)); }
  { Permission p(identity, 100); auto wrong = proof; wrong.identity.entry++;
    check(!p.consume(wrong, 50)); wrong = proof; wrong.identity.g1++;
    check(!p.consume(wrong, 50)); wrong = proof; wrong.g2 = wrong.identity.g1;
    check(!p.consume(wrong, 50)); }
  { Permission p(identity, 100); auto wrong = proof; wrong.identity.core++;
    check(!p.consume(wrong, 50)); wrong = proof; wrong.identity.controller++;
    check(!p.consume(wrong, 50)); wrong = proof; wrong.identity.binding++;
    check(!p.consume(wrong, 50)); wrong = proof; wrong.identity.path += "x";
    check(!p.consume(wrong, 50)); wrong = proof; wrong.identity.position += 1;
    check(!p.consume(wrong, 50)); wrong = proof; wrong.identity.playing = false;
    check(!p.consume(wrong, 50)); }
  { Permission p(identity, 100); auto missing = proof; missing.serviceClosed = false;
    check(!p.consume(missing, 50)); missing = proof; missing.retired = false;
    check(!p.consume(missing, 50)); missing = proof; missing.created = false;
    check(!p.consume(missing, 50)); missing = proof; missing.terminal = false;
    check(!p.consume(missing, 50)); missing = proof; missing.selectedFault = false;
    check(!p.consume(missing, 50)); missing = proof; missing.internalStop = false;
    check(!p.consume(missing, 50)); }
  { Permission p(identity, 100); p.cancel(); check(!p.consume(proof, 50)); }
  { Permission p(identity, 100); check(p.consume(proof, 50)); p.reloadResult(true); p.cancel();
    check(!p.mayContinue(identity, 6, 51)); }
  { Permission p(identity, 100); check(!p.consume(proof, 100)); }
  { Permission p(identity, 100); check(p.consume(proof, 50)); p.reloadResult(false);
    p.reloadResult(true); check(!p.mayContinue(identity, 6, 51)); check(!p.consume(proof, 52)); }
  if (failures) std::fprintf(stderr, "%d permission checks failed\n", failures);
  return failures ? 1 : 0;
}
