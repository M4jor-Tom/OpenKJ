set(CPM_DOWNLOAD_VERSION 0.43.1)
# SHA-256 of the CPM.cmake release asset for the version above. This file is
# include()d below, so an unverified download is arbitrary CMake code execution
# at configure time. Update both values together when bumping the version.
set(CPM_DOWNLOAD_HASH 1c40fc102ce9625d7de7eb14f541cab30cc3138dca627f0b0ec40293ce6c2934)

if(CPM_SOURCE_CACHE)
  # Expand relative path. This is important if the provided path contains a tilde (~)
  get_filename_component(CPM_SOURCE_CACHE ${CPM_SOURCE_CACHE} ABSOLUTE)
  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

# A previously downloaded file is re-verified rather than trusted on sight: a
# failed download leaves a zero-byte file that EXISTS reports as present, which
# would otherwise be include()d and silently reused forever.
if(EXISTS ${CPM_DOWNLOAD_LOCATION})
  file(SHA256 ${CPM_DOWNLOAD_LOCATION} CPM_EXISTING_HASH)
  if(NOT CPM_EXISTING_HASH STREQUAL CPM_DOWNLOAD_HASH)
    message(STATUS "Cached CPM.cmake failed verification, re-downloading")
    file(REMOVE ${CPM_DOWNLOAD_LOCATION})
  endif()
endif()

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
  message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
       https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
       ${CPM_DOWNLOAD_LOCATION}
       EXPECTED_HASH SHA256=${CPM_DOWNLOAD_HASH}
       # CMAKE_TLS_VERIFY only began defaulting to ON in CMake 3.31; this project
       # supports older versions, where the certificate would go unchecked.
       TLS_VERIFY ON
       STATUS CPM_DOWNLOAD_STATUS
  )
  list(GET CPM_DOWNLOAD_STATUS 0 CPM_DOWNLOAD_RESULT)
  if(NOT CPM_DOWNLOAD_RESULT EQUAL 0)
    list(GET CPM_DOWNLOAD_STATUS 1 CPM_DOWNLOAD_ERROR)
    file(REMOVE ${CPM_DOWNLOAD_LOCATION})
    message(FATAL_ERROR "Failed to download CPM.cmake: ${CPM_DOWNLOAD_ERROR}")
  endif()
endif()

include(${CPM_DOWNLOAD_LOCATION})
