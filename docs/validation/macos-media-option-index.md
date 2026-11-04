# Candidate formula build-option evidence

These lines are extracted from each pinned formula install method, not a
complete command: variables, standard arguments, conditional code and
helper methods must be resolved before execution. No commands were run.
Excluded HEAD/test/platform branches may still appear.

## autoconf

- `ENV["PERL"] = "/usr/bin/perl"`
- `system "./configure", "--prefix=#{prefix}", "--with-lispdir=#{elisp}"`
- `system "make", "install"`

## automake

- `ENV["PERL"] = "/usr/bin/perl" if OS.mac?`
- `system "./configure", "--sysconfdir=#{etc}",`
- `system "make", "install"`

## bison

- `system "./configure", "--disable-dependency-tracking",`
- `system "make", "install", "DESTDIR=#{buildpath}"`

## brotli

- `system "cmake", "-S", ".", "-B", "build", "-DCMAKE_INSTALL_RPATH=#{rpath}", *std_cmake_args`
- `system "cmake", "--build", "build", "--verbose"`
- `system "ctest", "--test-dir", "build", "--verbose"`
- `system "cmake", "--install", "build"`
- `system "cmake", "-S", ".", "-B", "build-static", "-DBUILD_SHARED_LIBS=OFF", *std_cmake_args`
- `system "cmake", "--build", "build-static"`

## ca-certificates

- No literal selected option or command detected; inspect formula.

## cairo

- `args = %w[`
- `-Dfontconfig=enabled`
- `-Dfreetype=enabled`
- `-Dpng=enabled`
- `-Dglib=enabled`
- `-Dxcb=enabled`
- `-Dxlib=enabled`
- `-Dzlib=enabled`
- `-Dglib=enabled`
- `args << "-Dquartz=enabled" if OS.mac?`
- `system "meson", "setup", "build", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## certifi

- `system python3, "-m", "pip", "install", *std_pip_args, "."`

## cffi

- `system python, "-m", "pip", "install", *std_pip_args(build_isolation: true), "."`

## cmake

- `args = %W[`
- `args += %w[`
- `system "./bootstrap", *args, "--", *std_cmake_args,`
- `system "make"`
- `system "make", "install"`

## dav1d

- `system "meson", "setup", "build", *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## deno

- `ENV["CARGO_FEATURE_SYSTEM"] = "1" # libffi`
- `ENV["LCMS2_LIB_DIR"] = formula_opt_lib("little-cms2")`
- `ENV["LIBSQLITE3_SYS_USE_PKG_CONFIG"] = "1"`
- `ENV["PYTHON"] = which("python3")`
- `ENV["NINJA"] = which("ninja")`
- `ENV["CLANG_BASE_PATH"] = llvm.prefix`
- `ENV["GN_ARGS"] = "clang_version=#{llvm.version.major} use_lld=#{OS.linux?}"`
- `system "cargo", "install", "--no-default-features", "-vv", *std_cargo_args(path: "cli", features:)`

## fast_float

- `system "cmake", "-S", ".", "-B", "build", *std_cmake_args`
- `system "cmake", "--install", "build"`

## ffmpeg

- `args = %W[`
- `--enable-shared`
- `--enable-pthreads`
- `--enable-version3`
- `--enable-ffplay`
- `--enable-gpl`
- `--enable-libsvtav1`
- `--enable-libopus`
- `--enable-libx264`
- `--enable-libmp3lame`
- `--enable-libdav1d`
- `--enable-libvmaf`
- `--enable-libvpx`
- `--enable-libx265`
- `--enable-openssl`
- `args += %w[--enable-videotoolbox --enable-audiotoolbox] if OS.mac?`
- `args << "--enable-neon" if Hardware::CPU.arm?`
- `system "./configure", *args`
- `system "make", "install"`
- `system "make", "alltools"`

## flac

- `system "./autogen.sh" if build.head?`
- `system "./configure", "--enable-static", *std_configure_args`
- `system "make", "install"`

## fontconfig

- `args = %W[`
- `-Ddoc=disabled`
- `-Dtests=disabled`
- `-Dtools=enabled`
- `-Dcache-build=disabled`
- `-Dadditional-fonts-dirs=no`
- `args << "-Ddefault-fonts-dirs=#{font_dirs}"`
- `system "meson", "setup", "build", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## freetype

- `system "./configure", "--prefix=#{prefix}",`
- `system "make"`
- `system "make", "install"`

## fribidi

- `system "./configure", "--disable-debug",`
- `system "make", "install"`

## gettext

- `ENV["am_cv_func_iconv_works"] = "yes" if OS.mac? && MacOS.version >= :sequoia`
- `args = [`
- `args << if OS.mac?`
- `system "./configure", *std_configure_args, *args`
- `system "make"`
- `system "make", "install"`

## giflib

- `args = ["PREFIX=#{prefix}"]`
- `args << "LIBUTILSO=" if OS.mac?`
- `system "make", "all", *args`
- `system "make", "install", *args`

## glib

- `args = %W[`
- `-Dgio_module_dir=#{HOMEBREW_PREFIX}/lib/gio/modules`
- `-Dbsymbolic_functions=false`
- `-Ddtrace=disabled`
- `-Druntime_dir=#{var}/run`
- `-Dtests=false`
- `system "meson", "setup", "build_staging", "-Dintrospection=disabled", *args, *staging_meson_args`
- `system "meson", "compile", "-C", "build_staging", "--verbose"`
- `system "meson", "install", "-C", "build_staging"`
- `system "meson", "setup", "build", "-Dcairo=disabled", "-Ddoctool=disabled", "-Dtests=false", *staging_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`
- `system "meson", "setup", "build", "--default-library=both", "-Dintrospection=enabled", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## gobject-introspection

- `ENV["GI_SCANNER_DISABLE_CACHE"] = "true"`
- `system "meson", "setup", "build", "-Dpython=#{venv.root}/bin/python",`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## graphite2

- `args = %W[`
- `-DCMAKE_INSTALL_RPATH=#{rpath}`
- `system "cmake", "-S", ".", "-B", "build", *args, *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## harfbuzz

- `args = %w[`
- `-Dcairo=enabled`
- `-Dcoretext=enabled`
- `-Dfreetype=enabled`
- `-Dglib=enabled`
- `-Dgobject=enabled`
- `-Dgraphite=enabled`
- `-Dicu=enabled`
- `-Dintrospection=enabled`
- `-Dtests=disabled`
- `system "meson", "setup", "build", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## help2man

- `system "perl", "Makefile.PL", "INSTALL_BASE=#{libexec}", "NO_MYMETA=1"`
- `system "make", "install"`
- `system "./configure", "--enable-nls", *std_configure_args`
- `system "make", "install"`

## icu4c@79

- `args = %w[`
- `--disable-samples`
- `--disable-tests`
- `--enable-static`
- `--with-library-bits=64`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## jpeg-turbo

- `args = ["-DWITH_JPEG8=1", "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,#{rpath}"]`
- `args += ["-DFLOATTEST8=fp-contract",`
- `args << "-DFLOATTEST12=no-fp-contract"`
- `args += std_cmake_args.reject { |arg| arg["CMAKE_INSTALL_LIBDIR"].present? }`
- `system "cmake", "-S", ".", "-B", "build", *args`
- `system "cmake", "--build", "build"`
- `system "ctest", "--test-dir", "build", "--rerun-failed", "--output-on-failure", "--parallel", ENV.make_jobs`
- `system "cmake", "--install", "build"`

## json-c

- `system "cmake", "-S", ".", "-B", "build", "-DBUILD_APPS=OFF", *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## lame

- `ENV["ac_cv_prog_cc_c23"] = "no"`
- `system "./configure", "--disable-dependency-tracking",`
- `system "make", "install"`

## libarchive

- `args = [`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## libass

- `args = OS.mac? ? ["--disable-fontconfig"] : []`
- `system "autoreconf", "--force", "--install", "--verbose" if build.head?`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## libb2

- `system "./configure", "--disable-dependency-tracking",`
- `system "make", "install"`

## libbluray

- `args = %w[`
- `-Dbdj_jar=disabled`
- `-Dfontconfig=enabled`
- `-Dfreetype=enabled`
- `-Dlibxml2=enabled`
- `system "meson", "setup", "build", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## libgit2

- `args = %w[`
- `-DBUILD_EXAMPLES=OFF`
- `-DBUILD_TESTS=OFF`
- `-DUSE_BUNDLED_ZLIB=OFF`
- `-DUSE_HTTPS=OpenSSL`
- `-DUSE_HTTP_PARSER=llhttp`
- `-DUSE_SSH=ON`
- `args << "-DREGEX_BACKEND=pcre2" if OS.linux?`
- `system "cmake", "-S", ".", "-B", "build", "-DBUILD_SHARED_LIBS=ON", *args, *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`
- `system "cmake", "-S", ".", "-B", "build-static", "-DBUILD_SHARED_LIBS=OFF", *args, *std_cmake_args`
- `system "cmake", "--build", "build-static"`

## libogg

- `system "cmake", "-S", ".", "-B", "build", "-DBUILD_SHARED_LIBS=TRUE", *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`
- `system "cmake", "-S", ".", "-B", "build-static", "-DBUILD_SHARED_LIBS=FALSE", *std_cmake_args`
- `system "cmake", "--build", "build-static"`

## libplacebo

- `args = %W[`
- `-Dlcms=enabled`
- `-Dshaderc=enabled`
- `-Dvulkan=enabled`
- `-Dvulkan-registry=#{Formula["vulkan-headers"].share}/vulkan/registry/vk.xml`
- `system "meson", "setup", "build", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## libpng

- `system "./configure", "--disable-silent-rules", *std_configure_args`
- `system "make"`
- `system "make", "test"`
- `system "make", "install"`

## libsamplerate

- `args = ["-DLIBSAMPLERATE_EXAMPLES=OFF"]`
- `system "cmake", "-S", ".", "-B", "build_shared", "-DBUILD_SHARED_LIBS=ON", *args, *std_cmake_args`
- `system "cmake", "--build", "build_shared"`
- `system "cmake", "--install", "build_shared"`
- `system "cmake", "-S", ".", "-B", "build_static", "-DBUILD_SHARED_LIBS=OFF", *args, *std_cmake_args`
- `system "cmake", "--build", "build_static"`

## libsndfile

- `args = %W[`
- `-DBUILD_PROGRAMS=ON`
- `-DENABLE_PACKAGE_CONFIG=ON`
- `-DINSTALL_PKGCONFIG_MODULE=ON`
- `-DBUILD_EXAMPLES=OFF`
- `-DCMAKE_INSTALL_RPATH=#{rpath}`
- `-DPYTHON_EXECUTABLE=#{which("python3")}`
- `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`
- `system "cmake", "-S", ".", "-B", "build", "-DBUILD_SHARED_LIBS=ON", *args, *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`
- `system "cmake", "-S", ".", "-B", "static", "-DBUILD_SHARED_LIBS=OFF", *args, *std_cmake_args`
- `system "cmake", "--build", "static"`

## libssh2

- `args = %W[`
- `--disable-silent-rules`
- `--disable-examples-build`
- `--with-openssl`
- `--with-libz`
- `--with-libssl-prefix=#{formula_opt_prefix("openssl@4")}`
- `system "./buildconf" if build.head?`
- `system "./configure", *std_configure_args, *args`
- `system "make", "install"`

## libtiff

- `args = %W[`
- `--disable-libdeflate`
- `--enable-webp`
- `--with-webp-include-dir=#{formula_opt_include("webp")}`
- `--with-webp-lib-dir=#{formula_opt_lib("webp")}`
- `--enable-zstd`
- `--enable-lzma`
- `--with-jpeg-include-dir=#{formula_opt_include("jpeg-turbo")}`
- `--with-jpeg-lib-dir=#{formula_opt_lib("jpeg-turbo")}`
- `--without-x`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## libtool

- `ENV["M4"] = formula_opt_bin("m4")/"m4"`
- `args = %w[`
- `--disable-silent-rules`
- `--enable-ltdl-install`
- `args << "--program-prefix=g" if OS.mac?`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## libudfread

- `system "meson", "setup", "build", *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## libunibreak

- `system "./configure", *std_configure_args, "--disable-silent-rules"`
- `system "make", "install"`

## libunistring

- `ENV["am_cv_func_iconv_works"] = "yes" if use_iconv_workaround`
- `system "./configure", "--disable-silent-rules", *std_configure_args`
- `system "make"`
- `system "make", "check" unless use_iconv_workaround`
- `system "make", "install"`

## libvmaf

- `system "meson", "setup", "build", "libvmaf", *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## libvorbis

- `system "./autogen.sh" if build.head?`
- `system "./configure", *std_configure_args`
- `system "make", "install"`

## libvpx

- `args = %W[`
- `--disable-dependency-tracking`
- `--disable-examples`
- `--disable-unit-tests`
- `--enable-pic`
- `--enable-runtime-cpu-detect`
- `--enable-shared`
- `--enable-vp9-highbitdepth`
- `args << "--target=#{Hardware::CPU.arch}-darwin#{OS.kernel_version.major}-gcc" if OS.mac?`
- `system "../configure", *args`
- `system "make", "install"`

## libx11

- `ENV["LC_CTYPE"] = "C"`
- `args = %W[`
- `--disable-silent-rules`
- `--enable-unix-transport`
- `--enable-tcp-transport`
- `--enable-ipv6`
- `--enable-xthreads`
- `--enable-specs=no`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## libxau

- `args = %W[`
- `--disable-silent-rules`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## libxcb

- `args = %W[`
- `--enable-dri3`
- `--enable-ge`
- `--enable-xevie`
- `--enable-xprint`
- `--enable-selinux`
- `--disable-silent-rules`
- `--enable-devel-docs=no`
- `--with-doxygen=no`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## libxdmcp

- `args = %W[`
- `--disable-silent-rules`
- `--enable-docs=no`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## libxext

- `args = %W[`
- `--disable-silent-rules`
- `--enable-specs=no`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## libxrender

- `args = %W[`
- `--disable-silent-rules`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## little-cms2

- `system "./configure", *std_configure_args`
- `system "make", "install"`

## lld

- `system "cmake", "-S", "lld", "-B", "build",`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## llhttp

- `system "cmake", "-S", ".", "-B", "build", *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## llvm

- `args = %W[`
- `-DLLVM_ENABLE_PROJECTS=#{projects.join(";")}`
- `-DLLVM_ENABLE_RUNTIMES=#{runtimes.join(";")}`
- `-DLLVM_POLLY_LINK_INTO_TOOLS=ON`
- `-DLLVM_BUILD_EXTERNAL_COMPILER_RT=ON`
- `-DLLVM_LINK_LLVM_DYLIB=ON`
- `-DLLVM_ENABLE_EH=OFF`
- `-DLLVM_ENABLE_FFI=ON`
- `-DLLVM_ENABLE_RTTI=ON`
- `-DLLVM_ENABLE_Z3_SOLVER=#{enable_z3 ? "ON" : "OFF"}`
- `-DLLVM_INCLUDE_BENCHMARKS=OFF`
- `-DLLVM_INCLUDE_DOCS=OFF`
- `-DLLVM_INCLUDE_TESTS=OFF`
- `-DLLVM_INSTALL_UTILS=ON`
- `-DLLVM_OPTIMIZED_TABLEGEN=ON`
- `-DLLVM_TARGETS_TO_BUILD=all`
- `-DLLVM_USE_RELATIVE_PATHS_IN_FILES=ON`
- `-DLLVM_SOURCE_PREFIX=.`
- `-DLIBOMP_INSTALL_ALIASES=OFF`
- `-DLIBCXX_INSTALL_MODULES=ON`
- `-DCLANG_PYTHON_BINDINGS_VERSIONS=#{python_versions.join(";")}`
- `-DLLVM_CREATE_XCODE_TOOLCHAIN=OFF`
- `-DCLANG_FORCE_MATCHING_LIBCLANG_SOVERSION=OFF`
- `-DCLANG_CONFIG_FILE_SYSTEM_DIR=../etc/clang`
- `-DCLANG_CONFIG_FILE_USER_DIR=~/.config/clang`
- `args += %W[`
- `-DPACKAGE_VENDOR=#{tap.user}`
- `-DBUG_REPORT_URL=#{tap.issues_url}`
- `args << "-DCLANG_VENDOR_UTI=sh.brew.clang" if tap.official?`
- `args << "-DFFI_INCLUDE_DIR=#{macos_sdk}/usr/include/ffi"`
- `args << "-DFFI_LIBRARY_DIR=#{macos_sdk}/usr/lib"`
- `args << "-DCLANG_USE_XCSELECT=ON"`
- `args << "-DLLVM_BUILD_LLVM_C_DYLIB=ON"`
- `args << "-DLLVM_ENABLE_LIBCXX=ON"`
- `args << "-DLIBCXX_ENABLE_VENDOR_AVAILABILITY_ANNOTATIONS=ON"`
- `args << "-DLIBCXX_PSTL_BACKEND=libdispatch"`
- `args << "-DLIBCXX_INSTALL_LIBRARY_DIR=#{libcxx_install_libdir}"`
- `args << "-DLIBUNWIND_INSTALL_LIBRARY_DIR=#{libunwind_install_libdir}"`
- `args << "-DLIBCXXABI_INSTALL_LIBRARY_DIR=#{libcxx_install_libdir}"`
- `args << "-DFFI_INCLUDE_DIR=#{formula_opt_include("libffi")}"`
- `args << "-DFFI_LIBRARY_DIR=#{formula_opt_lib("libffi")}"`
- `args << "-DLIBOMP_OMPD_GDB_SUPPORT=OFF"`
- `args << "-DLLVM_ENABLE_LIBXML2=OFF"`
- `args << "-DLLVM_ENABLE_LIBCXX=OFF"`
- `args << "-DCLANG_DEFAULT_CXX_STDLIB=libstdc++"`
- `args << "-DLLVM_BINUTILS_INCDIR=#{formula_opt_include("binutils")}"`
- `args << "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"`
- `-DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF`
- `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`
- `-DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON`
- `-DLIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY=OFF`
- `-DLIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY=ON`
- `-DLIBCXX_USE_COMPILER_RT=ON`
- `-DLIBCXX_HAS_ATOMIC_LIB=OFF`
- `-DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON`
- `-DLIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY=OFF`
- `-DLIBCXXABI_STATICALLY_LINK_UNWINDER_IN_STATIC_LIBRARY=ON`
- `-DLIBCXXABI_USE_COMPILER_RT=ON`
- `-DLIBCXXABI_USE_LLVM_UNWINDER=ON`
- `-DLIBUNWIND_USE_COMPILER_RT=ON`
- `-DCOMPILER_RT_USE_BUILTINS_LIBRARY=ON`
- `-DCOMPILER_RT_USE_LLVM_UNWINDER=ON`
- `-DSANITIZER_CXX_ABI=libc++`
- `-DSANITIZER_TEST_CXX=libc++`
- `-DLLVM_TARGETS_TO_BUILD=Native#{";AArch64;ARM;X86" if OS.mac?}`
- `-DLLVM_ENABLE_PROJECTS=clang;lld`
- `-DLLVM_ENABLE_RUNTIMES=compiler-rt`
- `args << "-DLLVM_ENABLE_LTO=Thin" if lto_build`
- `args << "-DCMAKE_LIBTOOL=#{stage1}/bin/llvm-libtool-darwin"`
- `system "cmake", "-S", llvmpath, "-B", stage1, "-G", "Ninja", *extra_args, *std_cmake_args`
- `system "cmake", "--build", stage1, "--target", *stage1_targets`
- `system "cmake", "-S", llvmpath, "-B", stage2, "-G", "Ninja",`
- `system "cmake", "--build", stage2, "--target", "clang", "lld", "runtimes"`
- `system "cmake", "--build", stage2, "--target", "check-clang", "check-llvm", "--", "-k", "0"`
- `system "cmake", "-S", llvmpath, "-B", stage2_profdata, "-G", "Ninja",`
- `system "cmake", "--build", stage2_profdata, "--", "-k", "0"`
- `system stage1/"bin/llvm-profdata", "merge", "-output=#{pgo_profile}", *profpath.glob("*.profraw")`
- `args << "-DCMAKE_C_COMPILER=#{stage1}/bin/clang"`
- `args << "-DCMAKE_CXX_COMPILER=#{stage1}/bin/clang++"`
- `args << "-DLLVM_PROFDATA_FILE=#{pgo_profile}"`
- `args << "-DCLANG_TABLEGEN=#{stage1}/bin/clang-tblgen"`
- `args << "-DCMAKE_C_FLAGS=#{ENV.cflags}"`
- `args << "-DCMAKE_CXX_FLAGS=#{ENV.cxxflags}"`
- `args << "-DRUNTIMES_CMAKE_ARGS=#{runtimes_cmake_args.join(";")}" if runtimes_cmake_args.present?`
- `args << "-DBUILTINS_CMAKE_ARGS=#{builtins_cmake_args.join(";")}" if builtins_cmake_args.present?`
- `system "cmake", "-S", llvmpath, "-B", "build", "-G", "Ninja", *args, *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--build", "build", "--target", "install"`
- `system "/usr/libexec/PlistBuddy", "-c", "Add:CFBundleIdentifier string org.llvm.#{llvm_version}", "Info.plist"`
- `system "/usr/libexec/PlistBuddy", "-c", "Add:CompatibilityVersion integer 2", "Info.plist"`
- `system bin/"llvm-ar", "xN", occurrences[member].to_s, static_archive, member,`
- `system bin/"llvm-ar", "x", static_archive`
- `system bin/"clang", "-fno-lto", "-Wno-unused-command-line-argument",`
- `system bin/"llvm-ar", "qcs", rebuilt_archive, *member_files`
- `system bin/"llvm-ar", "r", static_archive, *converted_files`

## luajit

- `ENV["MACOSX_DEPLOYMENT_TARGET"] = MacOS.version.to_s if OS.mac?`
- `system "make", "amalg", "PREFIX=#{HOMEBREW_PREFIX}", *verbose_args`
- `system "make", "install", "PREFIX=#{prefix}", *verbose_args`

## lz4

- `system "make", "install", "PREFIX=#{prefix}"`
- `ENV["DESTDIR"] = buildpath`
- `system "cmake", "-S", "build/cmake", "-B", "build",`
- `system "cmake", "--build", "build" # needed to run '--install' which rewrites build path in .cmake file`
- `system "cmake", "--install", "build"`

## lzo

- `system "./configure", "--disable-dependency-tracking",`
- `system "make"`
- `system "make", "check"`
- `system "make", "install"`

## m4

- `system "./configure", "--disable-dependency-tracking", "--prefix=#{prefix}"`
- `system "make"`
- `system "make", "install"`

## meson

- `system python3, "-m", "pip", "install", *std_pip_args(build_isolation: true), "."`

## molten-vk

- `system "cmake", "-S", ".", "-B", "build", *std_cmake_args`
- `system "cmake", "--build", "build"`
- `xcodebuild "ARCHS=#{Hardware::CPU.arch}", "ONLY_ACTIVE_ARCH=YES",`
- `xcodebuild "-create-xcframework", "-output", "./External/build/Release/SPIRVCross.xcframework",`
- `xcodebuild "-create-xcframework", "-output", "./External/build/Release/SPIRVTools.xcframework",`
- `xcodebuild "ARCHS=#{Hardware::CPU.arch}", "ONLY_ACTIVE_ARCH=YES",`

## mpdecimal

- `system "./configure", *std_configure_args`
- `system "make"`
- `system "make", "install"`

## mpg123

- `args = %w[`
- `--with-module-suffix=.so`
- `--enable-static`
- `args << "--with-default-audio=coreaudio" if OS.mac?`
- `args << if Hardware::CPU.arm?`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## mpv

- `args = %W[`
- `-Dbuild-date=false`
- `-Dhtml-build=enabled`
- `-Djavascript=enabled`
- `-Dlibmpv=true`
- `-Dlua=luajit`
- `-Dlibarchive=enabled`
- `-Duchardet=enabled`
- `-Dvulkan=enabled`
- `args += %w[`
- `-Degl=enabled`
- `-Dwayland=enabled`
- `-Dx11=enabled`
- `system "meson", "setup", "build", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## mujs

- `system "make", "prefix=#{prefix}", "release"`
- `system "make", "prefix=#{prefix}", "install"`
- `system "make", "prefix=#{prefix}", "install-shared" if build.stable?`

## nasm

- `system "./autogen.sh" if build.head?`
- `system "./configure", "--prefix=#{prefix}"`
- `system "make", "manpages" if build.head?`
- `system "make", "install"`

## ninja

- `system "python3", "configure.py", "--bootstrap", "--verbose", "--with-python=python3"`

## openssl@4

- `system "perl", "./Configure", *configure_args, *arch_args`
- `system "make"`
- `system "make", "install", "MANDIR=#{man}", "MANSUFFIX=ssl"`
- `system "make", "HARNESS_JOBS=#{ENV.make_jobs}", "test" if build.bottle?`

## opus

- `system "./autogen.sh" if build.head?`
- `system "./configure", "--disable-doc", *std_configure_args`
- `system "make", "install"`

## pcre2

- `args = %w[`
- `--enable-pcre2-16`
- `--enable-pcre2-32`
- `--enable-pcre2grep-libz`
- `--enable-pcre2grep-libbz2`
- `--enable-jit`
- `args << "--enable-pcre2test-libedit" if OS.mac?`
- `system "./autogen.sh" if build.head?`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## pixman

- `system "meson", "setup", "build", "--default-library=both", *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## pkgconf

- `args = ["-Dauto_features=disabled"]`
- `system "./bootstrap.sh", "build"`
- `system "build/muon-bootstrap", "meson", "setup", "build", *args, *std_meson_args(prefix: buildpath/"muon")`
- `system "build/muon-bootstrap", "-C", "build", "samu"`
- `system "build/muon", "-C", "build", "install"`
- `args = %W[`
- `-Dwith-pkg-config-dir=#{pc_path.uniq.join(File::PATH_SEPARATOR)}`
- `-Dwith-system-includedir=#{MacOS.sdk_path if OS.mac?}/usr/include`
- `-Dwith-system-libdir=/usr/lib`
- `system "muon", "meson", "setup", "build", *args, *std_meson_args`
- `system "muon", "-C", "build", "samu"`
- `system "muon", "-C", "build", "install"`
- `args = %W[`
- `--disable-silent-rules`
- `--with-pkg-config-dir=#{pc_path.uniq.join(File::PATH_SEPARATOR)}`
- `--with-system-includedir=#{MacOS.sdk_path if OS.mac?}/usr/include`
- `--with-system-libdir=/usr/lib`
- `system "./configure", *args, *std_configure_args`
- `system "make"`
- `system "make", "install"`

## pycparser

- `system python3, "-m", "pip", "install", *std_pip_args, "."`

## python-setuptools

- `system python3, "-m", "pip", "install", *std_pip_args, "."`

## python@3.14

- `ENV["PYTHONHOME"] = nil`
- `ENV["PYTHONPATH"] = nil`
- `args = %W[`
- `--enable-ipv6`
- `--without-ensurepip`
- `--enable-loadable-sqlite-extensions`
- `--with-openssl=#{formula_opt_prefix(openssl)}`
- `--enable-optimizations`
- `--with-system-expat`
- `--with-system-libmpdec`
- `--with-readline=editline`
- `args << "--with-lto"`
- `args << "--enable-framework=#{frameworks}"`
- `args << "--with-dtrace"`
- `args << "--with-dbmliborder=ndbm"`
- `args << "--with-tail-call-interp"`
- `args << "MACOSX_DEPLOYMENT_TARGET=#{MacOS.version}"`
- `args << "--enable-shared"`
- `args << "--with-dbmliborder="`
- `args << "CFLAGS=#{cflags.join(" ")}" unless cflags.empty?`
- `args << "CFLAGS_NODIST=#{cflags_nodist.join(" ")}" unless cflags_nodist.empty?`
- `args << "LDFLAGS=#{ldflags.join(" ")}" unless ldflags.empty?`
- `args << "LDFLAGS_NODIST=#{ldflags_nodist.join(" ")}" unless ldflags_nodist.empty?`
- `args << "CPPFLAGS=#{cppflags.join(" ")}" unless cppflags.empty?`
- `args += %w[`
- `system "./configure", *args`
- `system "make"`
- `system "make", target, "PYTHONAPPSDIR=#{prefix}"`
- `system "make", "frameworkinstallextras", "PYTHONAPPSDIR=#{pkgshare}" if OS.mac?`
- `system python3, "-m", "venv", whl_build`
- `system whl_build/"bin/pip3", "install", *common_pip_args, "."`
- `system whl_build/"bin/pip3", "wheel", *common_pip_args,`
- `system whl_build/"bin/pip3", "wheel", *common_pip_args,`
- `system python3, "-Im", "ensurepip"`
- `system python3, "-Im", "pip", "install", "-v",`

## python@3.15

- `ENV["PYTHONHOME"] = nil`
- `ENV["PYTHONPATH"] = nil`
- `args = %W[`
- `--enable-ipv6`
- `--without-ensurepip`
- `--enable-loadable-sqlite-extensions`
- `--with-openssl=#{formula_opt_prefix(openssl)}`
- `--enable-optimizations`
- `--with-system-expat`
- `--with-system-libmpdec`
- `--with-readline=editline`
- `args << "--with-lto"`
- `args << "--enable-framework=#{frameworks}"`
- `args << "--with-dtrace"`
- `args << "--with-dbmliborder=ndbm"`
- `args << "--with-tail-call-interp" if DevelopmentTools.clang_build_version >= 1700`
- `args << "MACOSX_DEPLOYMENT_TARGET=#{MacOS.version}"`
- `args << "--enable-shared"`
- `args << "--with-dbmliborder="`
- `args << "CFLAGS=#{cflags.join(" ")}" unless cflags.empty?`
- `args << "CFLAGS_NODIST=#{cflags_nodist.join(" ")}" unless cflags_nodist.empty?`
- `args << "LDFLAGS=#{ldflags.join(" ")}" unless ldflags.empty?`
- `args << "LDFLAGS_NODIST=#{ldflags_nodist.join(" ")}" unless ldflags_nodist.empty?`
- `args << "CPPFLAGS=#{cppflags.join(" ")}" unless cppflags.empty?`
- `args += %w[`
- `system "./configure", *args`
- `system "make"`
- `system "make", target, "PYTHONAPPSDIR=#{prefix}"`
- `system "make", "frameworkinstallextras", "PYTHONAPPSDIR=#{pkgshare}" if OS.mac?`
- `system python3, "-m", "venv", whl_build`
- `system whl_build/"bin/pip3", "install", *common_pip_args, "."`
- `system whl_build/"bin/pip3", "wheel", *common_pip_args,`
- `system python3, "-Im", "ensurepip"`
- `system python3, "-Im", "pip", "install", "-v",`

## readline

- `system "./configure", "--with-curses", *std_configure_args`
- `system "make", "install", "SHLIB_LIBS=-lcurses"`

## rubberband

- `args = ["-Dresampler=libsamplerate"]`
- `args << "-Dfft=fftw" if OS.linux?`
- `system "meson", "setup", "build", *args, *std_meson_args`
- `system "meson", "compile", "-C", "build", "--verbose"`
- `system "meson", "install", "-C", "build"`

## rust

- `ENV["OPENSSL_DIR"] = formula_opt_prefix(openssl)`
- `ENV["LIBGIT2_NO_VENDOR"] = "1"`
- `ENV["LIBSQLITE3_SYS_USE_PKG_CONFIG"] = "1"`
- `ENV["LIBSSH2_SYS_USE_PKG_CONFIG"] = "1"`
- `ENV["SDKROOT"] = MacOS.sdk_path`
- `ENV["MACOSX_DEPLOYMENT_TARGET"] = MacOS.version`
- `args = %W[`
- `--enable-llvm-link-shared`
- `--enable-profiler`
- `--enable-vendor`
- `--disable-cargo-native-static`
- `--disable-docs`
- `--disable-lld`
- `args += %w[`
- `system "./configure", *args`
- `system "make"`
- `system "make", "install"`

## sdl2-compat

- `args = ["-DCMAKE_INSTALL_RPATH=#{rpath(target: formula_opt_lib("sdl3"))}"] if OS.mac?`
- `system "cmake", "-S", ".", "-B", "build", *args, *std_cmake_args(install_prefix: HOMEBREW_PREFIX)`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build", "--prefix", prefix`

## sdl3

- `args = %w[`
- `-DSDL_TESTS=OFF`
- `-DSDL_X11_XTEST=OFF`
- `system "cmake", "-S", ".", "-B", "build", *args, *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## shaderc

- `system "cmake", "-S", ".", "-B", "build",`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## sqlite

- `-DSQLITE_ENABLE_API_ARMOR=1`
- `-DSQLITE_ENABLE_COLUMN_METADATA=1`
- `-DSQLITE_ENABLE_DBSTAT_VTAB=1`
- `-DSQLITE_ENABLE_FTS3=1`
- `-DSQLITE_ENABLE_FTS3_PARENTHESIS=1`
- `-DSQLITE_ENABLE_FTS5=1`
- `-DSQLITE_ENABLE_GEOPOLY=1`
- `-DSQLITE_ENABLE_JSON1=1`
- `-DSQLITE_ENABLE_MEMORY_MANAGEMENT=1`
- `-DSQLITE_ENABLE_RTREE=1`
- `-DSQLITE_ENABLE_STAT4=1`
- `-DSQLITE_ENABLE_UNLOCK_NOTIFY=1`
- `-DSQLITE_MAX_VARIABLE_NUMBER=250000`
- `-DSQLITE_USE_URI=1`
- `args = [`
- `args << "--soname=legacy" if OS.linux?`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## svt-av1

- `system "cmake", "-S", ".", "-B", "build", "-DCMAKE_INSTALL_RPATH=#{rpath}", *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## uchardet

- `system "cmake", "-S", ".", "-B", "build",`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## util-macros

- `args = %W[`
- `--disable-silent-rules`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## vapoursynth

- No literal selected option or command detected; inspect formula.

## vulkan-headers

- `system "cmake", "-S", ".", "-B", "build", *std_cmake_args`
- `system "cmake", "--install", "build"`

## vulkan-loader

- `system "cmake", "-S", ".", "-B", "build",`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`

## webp

- `args = %W[`
- `-DCMAKE_INSTALL_RPATH=#{rpath}`
- `-DCMAKE_DISABLE_FIND_PACKAGE_TIFF=ON`
- `system "cmake", "-S", ".", "-B", "build", *std_cmake_args, "-DBUILD_SHARED_LIBS=ON", *args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`
- `system "cmake", "-S", ".", "-B", "static", *std_cmake_args, "-DBUILD_SHARED_LIBS=OFF", *args`
- `system "cmake", "--build", "static"`

## x264

- `args = %W[`
- `--disable-lsmash`
- `--disable-swscale`
- `--disable-ffms`
- `--enable-shared`
- `--enable-static`
- `--enable-strip`
- `system "./configure", *args`
- `system "make", "install"`

## x265

- `args = %W[`
- `-DLINKED_10BIT=ON`
- `-DLINKED_12BIT=ON`
- `-DEXTRA_LINK_FLAGS=-L.`
- `-DEXTRA_LIB=x265_main10.a;x265_main12.a`
- `-DCMAKE_INSTALL_RPATH=#{rpath}`
- `args << "-DENABLE_SVE2=OFF" if OS.linux? && Hardware::CPU.arm?`
- `-DHIGH_BIT_DEPTH=ON -DEXPORT_C_API=OFF`
- `-DENABLE_SHARED=OFF -DENABLE_CLI=OFF`
- `system "cmake", "-S", buildpath/"source", "-B", "10bit",`
- `system "cmake", "--build", "10bit"`
- `system "cmake", "-S", buildpath/"source", "-B", "12bit",`
- `system "cmake", "--build", "12bit"`
- `system "cmake", "-S", buildpath/"source", "-B", "8bit", *args, *std_cmake_args`
- `system "cmake", "--build", "8bit"`
- `system "libtool", "-static", "-o", "libx265.a", "libx265_main.a",`
- `system "ar", "cr", "libx265.a", "libx265_main.a", "libx265_main10.a",`
- `system "ranlib", "libx265.a"`
- `system "make", "install"`

## xcb-proto

- `args = %W[`
- `--disable-silent-rules`
- `system "./configure", *args`
- `system "make"`
- `system "make", "install"`

## xorgproto

- `args = %W[`
- `--disable-silent-rules`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## xtrans

- `args = %W[`
- `--disable-silent-rules`
- `--enable-docs=no`
- `system "./configure", *args, *std_configure_args`
- `system "make", "install"`

## xz

- `system "./configure", "--disable-silent-rules", "--disable-nls", *std_configure_args`
- `system "make", "check"`
- `system "make", "install"`

## yt-dlp

- `system "make", "lazy-extractors", "pypi-files" if build.head?`

## z3

- `args = %W[`
- `-DZ3_LINK_TIME_OPTIMIZATION=ON`
- `-DZ3_INCLUDE_GIT_DESCRIBE=OFF`
- `-DZ3_INCLUDE_GIT_HASH=OFF`
- `-DZ3_INSTALL_PYTHON_BINDINGS=ON`
- `-DZ3_BUILD_EXECUTABLE=ON`
- `-DZ3_BUILD_TEST_EXECUTABLES=OFF`
- `-DZ3_BUILD_PYTHON_BINDINGS=ON`
- `-DZ3_BUILD_DOTNET_BINDINGS=OFF`
- `-DZ3_BUILD_JAVA_BINDINGS=OFF`
- `-DZ3_USE_LIB_GMP=OFF`
- `-DPYTHON_EXECUTABLE=#{python3}`
- `-DCMAKE_INSTALL_PYTHON_PKG_DIR=#{Language::Python.site_packages(python3)}`
- `system "cmake", "-S", ".", "-B", "build", *args, *std_cmake_args`
- `system "cmake", "--build", "build"`
- `system "cmake", "--install", "build"`
- `system "make", "-C", "contrib/qprofdiff"`

## zimg

- `system "./autogen.sh"`
- `system "./configure", "--prefix=#{prefix}"`
- `system "make", "install"`

## zstd

- `system "cmake", "-S", "build/cmake", "-B", "builddir",`
- `system "cmake", "--build", "builddir"`
- `system "cmake", "--install", "builddir"`
