project (evttools C)
cmake_minimum_required (VERSION 2.6)

# Default to 2.6.3 behaviour
cmake_policy (VERSION 2.6.3)

# Moar warnings
# NOTE: GCC 2.95 doesn't support -Wextra
if ("${CMAKE_C_COMPILER_ID}" MATCHES "GNU" OR CMAKE_COMPILER_IS_GNUC)
	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall")
#	set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra -Wno-unused-parameter")
endif ("${CMAKE_C_COMPILER_ID}" MATCHES "GNU" OR CMAKE_COMPILER_IS_GNUC)

# Version
set (project_VERSION_MAJOR "1")
set (project_VERSION_MINOR "0")
set (project_VERSION_PATCH "0")

# For custom modules
set (CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Test this machine
include (CheckIncludeFile)

CHECK_INCLUDE_FILE ("unistd.h" HAVE_UNISTD_H)

include (CheckFunctionExists)

# Microsoft nonsense
CHECK_FUNCTION_EXISTS ("_vsnprintf_s" HAVE__VSNPRINTF_S)
CHECK_FUNCTION_EXISTS ("_strtoi64" HAVE__STRTOI64)
CHECK_FUNCTION_EXISTS ("_atoi64" HAVE__ATOI64)
CHECK_FUNCTION_EXISTS ("_fileno" HAVE__FILENO)
CHECK_FUNCTION_EXISTS ("_fstati64" HAVE__FSTATI64)
CHECK_FUNCTION_EXISTS ("_filelengthi64" HAVE__FILELENGTHI64)
CHECK_FUNCTION_EXISTS ("_chsize" HAVE__CHSIZE)
CHECK_FUNCTION_EXISTS ("_chsize_s" HAVE__CHSIZE_S)
CHECK_FUNCTION_EXISTS ("_putenv" HAVE__PUTENV)
CHECK_FUNCTION_EXISTS ("_putenv_s" HAVE__PUTENV_S)
CHECK_FUNCTION_EXISTS ("_tzset" HAVE__TZSET)

CHECK_FUNCTION_EXISTS ("snprintf" HAVE_SNPRINTF)
CHECK_FUNCTION_EXISTS ("strtoll" HAVE_STRTOLL)
CHECK_FUNCTION_EXISTS ("fstat" HAVE_FSTAT)
CHECK_FUNCTION_EXISTS ("filelength" HAVE_FILELENGTH)
CHECK_FUNCTION_EXISTS ("strptime" HAVE_STRPTIME)
CHECK_FUNCTION_EXISTS ("ftruncate" HAVE_FTRUNCATE)
CHECK_FUNCTION_EXISTS ("setenv" HAVE_SETENV)
CHECK_FUNCTION_EXISTS ("putenv" HAVE_PUTENV)
CHECK_FUNCTION_EXISTS ("tzset" HAVE_TZSET)
CHECK_FUNCTION_EXISTS ("getopt" HAVE_GETOPT)

include (CheckCSourceCompiles)

CHECK_C_SOURCE_COMPILES (
"#include <time.h>
int main (void) {_mkgmtime(NULL); return 0;}"
HAVE__MKGMTIME)

#CHECK_C_SOURCE_RUNS (
#"#include <stdio.h>
#int main (void) {char b;
#return snprintf(&b, 1, "xyz") != 3;}
#SNPRINTF_RETURNS_REQUIRED_LENGTH)

# Localization
find_package (Gettext)
if (GETTEXT_FOUND)
	set (HAVE_GETTEXT true)
	file (GLOB project_TRANSLATIONS ${PROJECT_SOURCE_DIR}/po/*.po)
	GETTEXT_CREATE_TRANSLATIONS (
		${PROJECT_SOURCE_DIR}/po/${CMAKE_PROJECT_NAME}.pot
		ALL ${project_TRANSLATIONS})
endif (GETTEXT_FOUND)

# Dependencies
find_package (LibXtnd)
include_directories (${XTND_INCLUDE_DIRS})

# Generate a configure file
configure_file (${PROJECT_SOURCE_DIR}/config.h.in
	${PROJECT_BINARY_DIR}/config.h)
include_directories (${PROJECT_BINARY_DIR})

# Doxygen
find_package (Doxygen)
if (DOXYGEN_FOUND)
	configure_file (${PROJECT_SOURCE_DIR}/docs/Doxyfile.in
		${PROJECT_BINARY_DIR}/Doxyfile)
	add_custom_target (${CMAKE_PROJECT_NAME}_doxygen
		COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_BINARY_DIR}/Doxyfile
		COMMENT "Generating documentation" VERBATIM)
endif (DOXYGEN_FOUND)

# Install man pages
file (GLOB manuals "man/[^.]*")
foreach (man ${manuals})
	# Some files might need generating
	if (man MATCHES "\\.in$")
		string (REGEX REPLACE "\\.in$" "" manout "${man}")
		get_filename_component (manout ${manout} NAME)
		set (manout "${PROJECT_BINARY_DIR}/${manout}")
		configure_file ("${man}" "${manout}")

		set (man "${manout}")
	endif (man MATCHES "\\.in$")

	# Install the man pages where they belong
	string (REGEX REPLACE ".+\\.([1-9])$" "\\1" section "${man}")
	install (FILES "${man}" DESTINATION "share/man/man${section}")
endforeach (man manuals)

# Common source files
set (project_common_sources
	src/base64.c
	src/fileio.c
	src/evt.c
	src/widechar.c
	src/sid.c
	src/datastruct.c)
set (project_common_headers
	${CMAKE_BINARY_DIR}/config.h
	src/base64.h
	src/fileio.h
	src/evt.h
	src/widechar.h
	src/sid.h
	src/datastruct.h)

# Build executables
add_executable (evt2csv src/evt2csv.c
	${project_common_sources} ${project_common_headers})
target_link_libraries (evt2csv ${XTND_LIBRARIES})

#add_executable (csv2evt src/csv2evt.c
#	${project_common_sources} ${project_common_headers})
#target_link_libraries (csv2evt ${XTND_LIBRARIES})

# The files to be installed
install (TARGETS evt2csv DESTINATION "bin")
#install (TARGETS csv2evt DESTINATION "bin")

# Do some unit tests
option (BUILD_TESTING "Build tests" OFF)

if (BUILD_TESTING)
	enable_testing ()
	include_directories (${PROJECT_SOURCE_DIR})

	create_test_sourcelist (tests_sources testdriver.c
		tests/base64.c
		tests/evt.c
		tests/datastruct.c
		tests/sid.c
		tests/widechar.c)

	add_executable (testdriver ${tests_sources}
		${project_common_sources} ${project_common_headers})
	target_link_libraries (testdriver ${XTND_LIBRARIES})

	remove (tests_sources testdriver.c)
	foreach (test ${tests_sources})
		get_filename_component (testname ${test} NAME_WE)
		get_filename_component (testpath ${test} PATH)
		add_test (${testname} testdriver ${testpath}/${testname})
	endforeach (test ${tests_sources})
endif (BUILD_TESTING)

# CPack
set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tools for Windows .evt files")
set (CPACK_PACKAGE_VENDOR "Přemysl Janouch")
set (CPACK_PACKAGE_CONTACT "Premysl Janouch <p.janouch@gmail.com>")
set (CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set (CPACK_PACKAGE_VERSION_MAJOR ${project_VERSION_MAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${project_VERSION_MINOR})
set (CPACK_PACKAGE_VERSION_PATCH ${project_VERSION_PATCH})
set (CPACK_GENERATOR "TGZ;ZIP")
set (CPACK_PACKAGE_FILE_NAME
	"${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set (CPACK_PACKAGE_INSTALL_DIRECTORY
	"${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")
set (CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set (CPACK_SOURCE_IGNORE_FILES "/build;/.svn;/.git;/CMakeLists.txt.user")
set (CPACK_SOURCE_PACKAGE_FILE_NAME
	"${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")

include (CPack)

