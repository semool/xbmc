include ../Makefile.include

ifneq ($(shell test -f $(PREFIX)/share/config.site && echo 1),1)
  $(error Error: $(PREFIX)/share/config.site  is missing. Please reconfigure depends to generate it)
endif

# Keep in alphabetical order
DEPENDS = \
  bzip2 \
  dav1d \
  expat \
  ffmpeg \
  fontconfig \
  freetype2 \
  freetype2-noharfbuzz \
  fribidi \
  fstrcmp \
  gettext \
  gmp \
  gnutls \
  gtest \
  harfbuzz \
  libbluray \
  libffi \
  libgcrypt \
  libgpg-error \
  libjpeg-turbo \
  liblzo2 \
  libmicrohttpd \
  libplist \
  libpng \
  libshairplay \
  libusb \
  libxml2 \
  mariadb \
  nettle \
  openssl \
  python3 \
  pythonmodule-pil \
  pythonmodule-pycryptodome \
  pythonmodule-setuptools \
  sqlite3 \
  tinyxml \
  udfread \
  xz

ifeq ($(ENABLE_GPLV3),yes)
  DEPENDS+=samba-gplv3 libcdio-gplv3
else
  DEPENDS+=samba libcdio
endif

FONTCONFIG=fontconfig
ifeq ($(OS),darwin_embedded)
  EXCLUDED_DEPENDS = libusb gtest libcdio libcdio-gplv3
  FONTCONFIG=
  ifeq ($(TARGET_PLATFORM),appletvos)
    EXCLUDED_DEPENDS += libshairplay libplist
  endif
  DEPENDS += darwin-embedded-entitlements
endif

ifeq ($(OS),osx)
  FONTCONFIG=
  EXCLUDED_DEPENDS = libusb
  DEPENDS += smctemp libaacs libbdplus apache-ant
  LIBAACS = libaacs
  LIBDPLUS = libbdplus
  APACHEANT = apache-ant
endif

ifeq ($(OS),android)
  EXCLUDED_DEPENDS = libusb gtest libcdio libcdio-gplv3
  DEPENDS += dummy-libxbmc libdovi libuuid
  PYMODULE_DEPS = dummy-libxbmc
  LIBUUID = libuuid
endif

ZLIB=
ifneq ($(HAS_ZLIB),1)
  DEPENDS += zlib
  ZLIB = zlib
endif

ICONV=
ifeq ($(NEED_LIBICONV),1)
  DEPENDS += libiconv
  ICONV = libiconv
endif

ALSA_LIB=
ifeq ($(OS),linux)
  DEPENDS += dbus libuuid alsa-lib libdrm libxkbcommon libinput libudev libevdev mtdev pipewire
  ALSA_LIB = alsa-lib
  LIBUUID = libuuid

  ifeq ($(CPU),x86_64)
    DEPENDS += libva
    LIBVA = libva
  endif

  ifneq (,$(findstring wayland,$(TARGET_PLATFORM)))
    DEPENDS += wayland waylandpp wayland-protocols
    MESA_DEPS += wayland-protocols wayland
    ifeq ($(CPU),x86_64)
      LIBVA_DEPS += wayland
    endif

    ifeq ($(RENDER_SYSTEM),gl)
      WAYLANDPP_DEPS += linux-system-gl-libs
    else
      WAYLANDPP_DEPS += mesa
    endif
  endif

  ifneq (,$(findstring x11,$(TARGET_PLATFORM)))
    DEPENDS += linux-system-x11-libs
    MESA_DEPS += linux-system-x11-libs
    ifeq ($(CPU),x86_64)
      LIBVA_DEPS += linux-system-x11-libs
    endif
  endif

  ifeq ($(RENDER_SYSTEM),gl)
    DEPENDS += linux-system-gl-libs
  else
    DEPENDS += mesa
  endif

  ifeq ($(TARGET_PLATFORM),webos)
    DEPENDS += libdovi wayland waylandpp wayland-protocols webos-wayland-extensions webos-userland
    EXCLUDED_DEPENDS += dbus linux-system-x11-libs pipewire mesa
  endif

  ifneq (,$(findstring gbm,$(TARGET_PLATFORM)))
    DEPENDS += hwdata libdisplay-info
  endif
endif
DEPENDS := $(filter-out $(EXCLUDED_DEPENDS),$(DEPENDS))

.PHONY: $(DEPENDS) download $(DOWNLOAD_TARGETS)

all: .installed-$(PLATFORM)

EXCLUDE := linux-system-gl-libs linux-system-x11-libs
DOWNLOAD_DEPS := $(filter-out $(EXCLUDE:%=%), $(DEPENDS))
DOWNLOAD_TARGETS := $(addprefix download/, $(DOWNLOAD_DEPS))

$(DOWNLOAD_TARGETS):
	$(MAKE) -C $(notdir $@) download

download: $(DOWNLOAD_TARGETS)

cec: p8-platform
crossguid: $(LIBUUID)
curl: brotli openssl nghttp2 $(ZLIB)
dbus: expat
exiv2: $(ICONV) $(ZLIB) expat
ffmpeg: $(ICONV) $(ZLIB) bzip2 gnutls dav1d $(LIBVA)
fontconfig: freetype2 expat $(ICONV) $(LIBUUID)
freetype2: bzip2 harfbuzz $(ZLIB)
gettext: $(ICONV)
gnutls: nettle $(ZLIB)
harfbuzz: freetype2-noharfbuzz $(ICONV)
libaacs: libgcrypt libgpg-error
libass: $(FONTCONFIG) fribidi harfbuzz freetype2 $(ICONV)
libbdplus: libaacs libgcrypt libgpg-error
libbluray: fontconfig freetype2 $(ICONV) udfread libxml2 $(LIBAACS) $(LIBDPLUS) $(APACHEANT)
libcdio-gplv3: $(ICONV)
libcdio: $(ICONV)
libdisplay-info: hwdata
libevdev: libudev
libgcrypt: libgpg-error
libinput: mtdev libevdev
libmicrohttpd: gnutls libgcrypt libgpg-error
libplist: $(ZLIB)
libpng: $(ZLIB)
libva: libdrm $(LIBVA_DEPS)
libxml2: $(ZLIB)
libxslt: libxml2
libzip: bzip2 gnutls $(ZLIB)
mariadb: openssl $(ICONV) $(ZLIB)
mesa: libdrm $(MESA_DEPS) $(ZLIB)
nettle: gmp
openssl: $(ZLIB)
python3: expat gettext libxml2 sqlite3 openssl libffi bzip2 xz $(ICONV)
pythonmodule-pil: bzip2 $(PYMODULE_DEPS) $(ZLIB) libjpeg-turbo libpng freetype2 python3 pythonmodule-setuptools
pythonmodule-pycryptodome: $(PYMODULE_DEPS) python3 pythonmodule-setuptools
pythonmodule-setuptools: $(PYMODULE_DEPS) python3
samba-gplv3: gnutls $(ZLIB)
taglib: utf8-cpp $(ZLIB)
wayland: expat libffi
waylandpp: wayland $(WAYLANDPP_DEPS)
xz: gettext

.installed-$(PLATFORM): $(DEPENDS)
	touch $@
	@echo "Dependencies built successfully."

$(DEPENDS):
	$(MAKE) -C $@

clean:
	for d in $(DEPENDS); do $(MAKE) -C $$d clean; done

# Debug target, this will DELETE all data in staging!
test-dependencies:
	( for d in $(DEPENDS); do \
	rm -rf $(PREFIX); \
	mkdir -p $(PREFIX)/include $(PREFIX)/share $(PREFIX)/bin; \
	cp -f config.site Toolchain.cmake $(PREFIX)/share/; \
	$(MAKE) distclean; \
	$(MAKE) $$d; done ) && echo "$@ built successfully"


distclean::
	for d in $(DEPENDS); do $(MAKE) -C $$d distclean; done

HOST_LIB_PKGCONFIG := $(firstword $(wildcard /usr/lib64/pkgconfig /usr/lib/$(HOST)/pkgconfig))

linux-system-x11-libs:
	mkdir -p $(PREFIX)/lib/pkgconfig
	[ -f $(PREFIX)/lib/pkgconfig/damageproto.pc ] || ln -sf  /usr/share/pkgconfig/damageproto.pc $(PREFIX)/lib/pkgconfig/damageproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/fixesproto.pc ] || ln -sf  /usr/share/pkgconfig/fixesproto.pc $(PREFIX)/lib/pkgconfig/fixesproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/x11-xcb.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/x11-xcb.pc $(PREFIX)/lib/pkgconfig/x11-xcb.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-dri2.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xcb-dri2.pc $(PREFIX)/lib/pkgconfig/xcb-dri2.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-dri3.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xcb-dri3.pc $(PREFIX)/lib/pkgconfig/xcb-dri3.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-glx.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xcb-glx.pc $(PREFIX)/lib/pkgconfig/xcb-glx.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-xfixes.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xcb-xfixes.pc $(PREFIX)/lib/pkgconfig/xcb-xfixes.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-present.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xcb-present.pc $(PREFIX)/lib/pkgconfig/xcb-present.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-randr.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xcb-randr.pc $(PREFIX)/lib/pkgconfig/xcb-randr.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-render.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xcb-render.pc $(PREFIX)/lib/pkgconfig/xcb-render.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-shape.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xcb-shape.pc $(PREFIX)/lib/pkgconfig/xcb-shape.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb-sync.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xcb-sync.pc $(PREFIX)/lib/pkgconfig/xcb-sync.pc
	[ -f $(PREFIX)/lib/pkgconfig/xdamage.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xdamage.pc $(PREFIX)/lib/pkgconfig/xdamage.pc
	[ -f $(PREFIX)/lib/pkgconfig/xf86vidmodeproto.pc ] || ln -sf /usr/share/pkgconfig/xf86vidmodeproto.pc $(PREFIX)/lib/pkgconfig/xf86vidmodeproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/xfixes.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xfixes.pc $(PREFIX)/lib/pkgconfig/xfixes.pc
	[ -f $(PREFIX)/lib/pkgconfig/xshmfence.pc ] || ln -sf  $(HOST_LIB_PKGCONFIG)/xshmfence.pc $(PREFIX)/lib/pkgconfig/xshmfence.pc
	[ -f $(PREFIX)/lib/pkgconfig/xxf86vm.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xxf86vm.pc $(PREFIX)/lib/pkgconfig/xxf86vm.pc
	[ -f $(PREFIX)/lib/pkgconfig/x11.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/x11.pc $(PREFIX)/lib/pkgconfig/x11.pc
	[ -f $(PREFIX)/lib/pkgconfig/xproto.pc ] || ln -sf /usr/share/pkgconfig/xproto.pc $(PREFIX)/lib/pkgconfig/xproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/kbproto.pc ] || ln -sf /usr/share/pkgconfig/kbproto.pc $(PREFIX)/lib/pkgconfig/kbproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/xcb.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xcb.pc $(PREFIX)/lib/pkgconfig/xcb.pc
	[ -f $(PREFIX)/lib/pkgconfig/pthread-stubs.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/pthread-stubs.pc $(PREFIX)/lib/pkgconfig/pthread-stubs.pc
	[ -f $(PREFIX)/lib/pkgconfig/xau.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xau.pc $(PREFIX)/lib/pkgconfig/xau.pc
	[ -f $(PREFIX)/lib/pkgconfig/xdmcp.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xdmcp.pc $(PREFIX)/lib/pkgconfig/xdmcp.pc
	[ -f $(PREFIX)/lib/pkgconfig/xext.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xext.pc $(PREFIX)/lib/pkgconfig/xext.pc
	[ -f $(PREFIX)/lib/pkgconfig/xextproto.pc ] || ln -sf /usr/share/pkgconfig/xextproto.pc $(PREFIX)/lib/pkgconfig/xextproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/xrandr.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xrandr.pc $(PREFIX)/lib/pkgconfig/xrandr.pc
	[ -f $(PREFIX)/lib/pkgconfig/xrender.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xrender.pc $(PREFIX)/lib/pkgconfig/xrender.pc
	[ -f $(PREFIX)/lib/pkgconfig/randrproto.pc ] || ln -sf /usr/share/pkgconfig/randrproto.pc $(PREFIX)/lib/pkgconfig/randrproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/renderproto.pc ] || ln -sf /usr/share/pkgconfig/renderproto.pc $(PREFIX)/lib/pkgconfig/renderproto.pc
	[ -f $(PREFIX)/lib/pkgconfig/xt.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xt.pc $(PREFIX)/lib/pkgconfig/xt.pc
	[ -f $(PREFIX)/lib/pkgconfig/ice.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/ice.pc $(PREFIX)/lib/pkgconfig/ice.pc
	[ -f $(PREFIX)/lib/pkgconfig/sm.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/sm.pc $(PREFIX)/lib/pkgconfig/sm.pc
	[ -f $(PREFIX)/lib/pkgconfig/xmu.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/xmu.pc $(PREFIX)/lib/pkgconfig/xmu.pc

linux-system-gl-libs:
	mkdir -p $(PREFIX)/lib/pkgconfig
	[ -f $(PREFIX)/lib/pkgconfig/egl.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/egl.pc $(PREFIX)/lib/pkgconfig/egl.pc
	[ -f $(PREFIX)/lib/pkgconfig/wayland-egl.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/wayland-egl.pc $(PREFIX)/lib/pkgconfig/wayland-egl.pc
	[ -f $(PREFIX)/lib/pkgconfig/gl.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/gl.pc $(PREFIX)/lib/pkgconfig/gl.pc
	[ -f $(PREFIX)/lib/pkgconfig/glu.pc ] || ln -sf $(HOST_LIB_PKGCONFIG)/glu.pc $(PREFIX)/lib/pkgconfig/glu.pc
