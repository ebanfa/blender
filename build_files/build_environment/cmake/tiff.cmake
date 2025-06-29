# SPDX-FileCopyrightText: 2002-2022 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(TIFF_EXTRA_ARGS
  -DZLIB_LIBRARY=${LIBDIR}/zlib/lib/${ZLIB_LIBRARY}
  -DZLIB_INCLUDE_DIR=${LIBDIR}/zlib/include
  -DJPEG_LIBRARY=${LIBDIR}/jpeg/lib/${JPEG_LIBRARY}
  -DJPEG_INCLUDE_DIR=${LIBDIR}/jpeg/include
  -DPNG_STATIC=ON
  -DBUILD_SHARED_LIBS=OFF
  -Dlzma=OFF
  -Djbig=OFF
  -Dzstd=OFF
  -Dwebp=OFF
  -Dlerc=OFF
  -Dtiff-tests=OFF
  -Dsphinx=OFF
)

if(APPLE)
  set(TIFF_EXTRA_ARGS
    ${TIFF_EXTRA_ARGS}
    # Work around issue where homebrew's libdeflate can be prioritized over our own dependency during linking if installed.
    -DDeflate_LIBRARY=${LIBDIR}/deflate/lib/libdeflate${LIBEXT}
  )
endif()

ExternalProject_Add(external_tiff
  URL file://${PACKAGE_DIR}/${TIFF_FILE}
  DOWNLOAD_DIR ${DOWNLOAD_DIR}
  URL_HASH ${TIFF_HASH_TYPE}=${TIFF_HASH}
  PREFIX ${BUILD_DIR}/tiff
  CMAKE_GENERATOR ${PLATFORM_ALT_GENERATOR}
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${LIBDIR}/tiff
    ${DEFAULT_CMAKE_FLAGS}
    ${TIFF_EXTRA_ARGS}

  INSTALL_DIR ${LIBDIR}/tiff
)

add_dependencies(
  external_tiff
  external_zlib
  external_jpeg
)
if(WIN32)
  if(BUILD_MODE STREQUAL Release)
    ExternalProject_Add_Step(external_tiff after_install
      COMMAND
        ${CMAKE_COMMAND} -E copy
          ${LIBDIR}/tiff/lib/tiff.lib
          ${HARVEST_TARGET}/tiff/lib/libtiff.lib &&
        ${CMAKE_COMMAND} -E copy_directory
          ${LIBDIR}/tiff/include/
          ${HARVEST_TARGET}/tiff/include/
      DEPENDEES install
    )
  endif()
else()
  harvest(external_tiff tiff/include tiff/include "*.h")
  harvest(external_tiff tiff/lib tiff/lib "*.a")
endif()
