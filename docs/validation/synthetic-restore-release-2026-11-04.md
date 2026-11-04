# Synthetic restoration: Release diagnostic failure, 2026-11-04

One contained Windows Release diagnostic attempt failed. This is fixture evidence, not a diagnosis of a production failure or proof of recovery. No unchanged retry or Debug attempt was performed.

The executable was built from `e2b82dc3271fd4939eea8ba0a80b00e22b82181a`; the orchestration checkout was `2ac2085a85f86ce3a1dc973004dc4e7d9bd87c42`, with no differences in the checked diagnostic build inputs. The Release fixture SHA-256 was `5a56ceb21a73618375ef77383d07d0a2b4ba9bb5b4648eea7d26295be1cf3f02`; the hidden-desktop helper was `706e0a939b4dff4b2f2996996863943c3d3a5dfba6dc46e96578f4fb73bd7a42`.

The original quiescence verdict was false at `service-wait`, with reason `wait-timeout`. The bounded observation recorded 929 sequential predicate samples; its retained first and last samples both report `pending: 0` and `result_predicate: false`. Intermediate sample values were not retained individually. The retained first and last samples short-circuited at that field, so this evidence does not establish the values of later service fields. It narrows the observed failure phase but does not establish why those samples recorded a zero pending counter.

The record shows one matching typed G1 render fault, accepted Stop, and a successful terminal observation: idle active, current VO unavailable, force-window `no`, and the expected canceled event with the binding retained. The accepted raw end event identifies entry 1, Stop reason, error 0, and no insertion. The record does not reach the service-close or retirement phases; G1 retirement and G2 restoration were not established. The fixture reported failure at `control.quiesce`; a complete QTest footer was absent.

The application exited naturally with code 1 and the helper with code 7. The helper recorded a known job count of zero, no termination request, and no forced cleanup. The runner correctly recorded failure. No claim of successful evidence flush is inferred solely from a field describing an attempted write.

Retained evidence SHA-256:

| Evidence | SHA-256 |
| --- | --- |
| Quiescence observation JSON | `61417c7d14f33d02a6d0b25ff79f49838b656f2413bf1b9501fb9f9a97b0cc7a` |
| QTest log | `7ccb1af40f4d64ad15cbdcd15dc2b00d27beb41142463580fb005a7348199f27` |
| Helper output | `4f6714eda7de6c9936f3a464920cbc1fba71ac7d030157bc353eb5b6af28020f` |
| Runner observation JSON | `8e7ef7fe04c15c9763e575847b7119ae362d1ebf59df9bafadec44bc904a85f1` |
