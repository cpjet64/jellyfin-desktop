# Recovery source assembly

This branch is a source-review candidate based on `4453818fcdeae3152fd07f29031ec6f3eb3b1670`. Its merge commits preserve the following production histories and fixed corrective inputs:

| Area | Original history | Captured corrective input |
| --- | --- | --- |
| Player actor | `e87af75d` | `3de8436cfd3b994826db10abcc89e5d5ccf1d645` |
| Renderer owner | `a3474c5e` | `d91ea955df28469e2019b5e3b6417ab1564d73d0` |
| Native bridge and planning | `4700a8ff` | `9ee470910a23a2412b92825aa6d5f05675e9be29` |
| Native seek identity | `d9a0ecb7076da671f17651437c0c0d377a3969fc` | `1cbafe8dac8f2f5d8dbb7e0e3619aa15a89b4b59` |
| MpvQt integration | `13bf4605` | `82d01ea447c9b2471850f5ef4b30611fb8ddc97a` |
| Recovery regression source | `7f561998` | `e2c8122256001449c8d40d586f24470bf218e3fb` |
| Owner tests and headless registration | — | `74ebc353e7c11b6c3af3aa9eeb91455397809856` |
| Native build source and verification gate | — | `468fe8c280dcbfde01fd5c08870271c0ed66f0b4` |
| QtBase | `43cfa393` | `43cfa39382eaf82020596b806a70330ab2ea226c` |
| QtQuick | `adb50e49` | `d36a3c04ff5124a06e69d9c7174468fb0dbba31d` |

The native patch series is `0001-recovery-frame-observation.patch`, `0002-recovery-seek-identity.patch`, then `0003-recovery-seek-epoch-tests.patch`.

Static copy verification applied the MpvQt event ownership, recovery owner, renderer recovery, and controller lease patches to pinned `2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e`. All five recovery and two controller normalized output hashes matched their assertions. This check does not compile those sources.

The qualified supervisor evidence at `d600fc2f6ffbeff419ffe627725b5634c1d6c1ce`, proposed compiler enrollment at `745f5903b7c92d46582f846b21978a51542f3dc2`, metadata preflight inputs at `3fff68c5c5454a46eb09bb5d0c18eca54fe34d76` and evidence at `82990f7c6a89775f08c76d17ee0deff94b367f36`, and first compiler enrollment packet at `5ab5476ca30ff3af4c49a098aba4d52559c57d41` are separate inputs, not production histories merged into this branch. Refer to their respective records for their precise scope and status.

The following first-parent merge commits preserve the exact input histories; the first parent is the immediately preceding assembly commit.

| Assembly merge | Merged input |
| --- | --- |
| `580d3667e51828b22872aec17c1e79101402573e` | `468fe8c280dcbfde01fd5c08870271c0ed66f0b4` |
| `870007e21da885ac99141ae4935aeeb702b78a07` | `82d01ea447c9b2471850f5ef4b30611fb8ddc97a` |
| `ebfe672832eb3147b0b9b8556d885ad959d584b9` | `73968edc0036043e4930cad314dd39919ddd009f` |
| `421fb308cc3003233e1d2b6cfbd969486357c2a3` | `74ebc353e7c11b6c3af3aa9eeb91455397809856` |
| `689561df920516836da9adb8124b6f111f15de29` | `d91ea955df28469e2019b5e3b6417ab1564d73d0` |
| `4f15526689da0a1393fda12164b0de0192b8a7d3` | `3de8436cfd3b994826db10abcc89e5d5ccf1d645` |
| `998f3a2868d104f1420556fffbb193133c8ebf70` | `d4d592b757a6146004cc9bbad038e44212b24aa3` |
| `89fff0f57c602c922473207b9c9b364fb38542ec` | `cf5a16492cc171d769ca6107704b5fe03b36da81` |
| `35373de1f957422716b47527ddf1578a780dff82` | `2020f2ae6ee383b6c16469ce476eb0fdc528f466` |
| `9b21d1343454c267d01be66162f49c7157a1a328` | `1cbafe8dac8f2f5d8dbb7e0e3619aa15a89b4b59` |
| `532ac42524e503375c6bd925e7d603d2de82c174` | `fd01e10d2576e60091f6a6d39d5a34ccda50ed13` |
| `5369355f6b584a837a5ebb3efb0c354777b7d9b3` | `bdf0ebbfc092c219bddc91c7773b4dd8a0cc2bcd` |
| `32ace4ed617e2bb1ed038e5a7fd4cecccce874b3` | `41570b9c4c7880014889c9a7bf997070bbd36135` |
| `c4d254059a417bdde199bace4c997ed9c3508a6c` | `bea23c4540b567c7b87a933852b2bfdfe74b7e2a` |
| `8cecae4f116bc03b70cfd4f33c7bdb2921e4a876` | `4640fe1e125e4f5664e6afcd1d712fa5eb6c8c2f` |
| `2ff81886418313683a06ba58324ef26804c71a51` | `7ad98813a37e93af43db914e4be9b403e06d897f` |
| `79871b8da0570170781969ff44fc507521bf11b3` | `6a137f64cb0a6ecacc8b1fac6aa7d51427f1fbc4` |
| `7dfc6567caf915a0badd0bf3e2924c0b0f40af0e` | `d36a3c04ff5124a06e69d9c7174468fb0dbba31d` |
| `764059a1cc08b39008dc1d2e2d8e80364be52688` | `8f410dc4227da0e0ec781d4d1bb63d77c4029057` |
| `dff6a1a45149bb2293355f0f4f46e7a8404af12f` | `0a955cc425b86bbae1202c36562c6f05f5c80280` |
| `7661df255166faa4275b57669686db35e7ea9b2e` | `0a67c55a30ae550cfaf0243de434d6cbed3507da` |
| `dc1601d2d6eddf7c77df6219edb99d494949de4c` | `e2c8122256001449c8d40d586f24470bf218e3fb` |
| `c85e238be74243a614af531b55b36b1cbeb8e3fe` | `cef936c3c820da88dfdcee8f6c5cf42e8b8baf4a` |
| `b46f5addd51a7182b04eed0eda0bd24013e0118e` | `56c1ff3901ed4cc782fc79362b87849a26cb92a9` |
| `8b3b1b97f25e93d61d5ce86fe5f66e208a35e300` | `43cfa39382eaf82020596b806a70330ab2ea226c` |
| `6cb666dfdba5d08b3c508219d7e3f02d5d764026` | `8b1977546c857c0848de88f60fdaedc4ad3e9950` |
| `76a49ab7823f7969eeffeedba7141abdd2a1217c` | `d9afc4ba402e03b5f8962651eaa5c0112c06c891` |
| `04ea8ff6c7b39e483fd2e4d38569d746adb4a899` | `9ee470910a23a2412b92825aa6d5f05675e9be29` |
| `e618b9f888fb964a86597f353e84f293f6c01a3f` | `a3474c5e425959b321e80ee47fd709dd9223298f` |
| `f07c392f546512a50e6da815c3e023a37e5f2765` | `3b6236ad8e13f757bf8fdef509166528d22e8604` |

This assembly does not establish compilation, runtime recovery, release readiness, or deployment. Compiler execution requires review of the concrete enrollment; CMake, tests, application execution, CI, release, and deployment remain held.

The paused single-frame correction has pure-gate source coverage; an actor integration case remains outstanding. The native lock and producer recipe remain incomplete, so a verified artifact, producer provenance, private Qt artifact, actual headless and GUI cases, and runtime behavior are not established. The recovery kit gate defaults off and enables the private ABI only after its strict Windows bundled configuration and artifact checks; the gate has not been executed here.
