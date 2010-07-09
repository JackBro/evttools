# - Find GNU gettext tools
# This module looks for the GNU gettext tools. This module defines the
# following values:
#    GETTEXT_MSGMERGE_EXECUTABLE: the full path to the msgmerge tool.
#    GETTEXT_MSGFMT_EXECUTABLE: the full path to the msgfmt tool.
#    GETTEXT_FOUND: True if gettext has been found.
#
# Additionally it provides the following macros:
# GETTEXT_CREATE_TRANSLATIONS ( outputFile [ALL] file1 ... fileN )
#    This will create a target "${PROJECT_NAME}_translations" which will
#    convert the given input po files into the binary output mo file.
#    If the ALL option is used, the translations will also be created
#    when building the default target.

#=============================================================================
# Copyright 2007-2009 Kitware, Inc.
# Copyright 2010 Přemysl Janouch
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the Kitware nor the names of contributors may be
#       used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.
#=============================================================================

find_program (GETTEXT_MSGMERGE_EXECUTABLE msgmerge)

find_program (GETTEXT_MSGFMT_EXECUTABLE msgfmt)

macro (GETTEXT_CREATE_TRANSLATIONS _potFile _firstPoFileArg)
	# Make it a real variable, so we can modify it here.
	set (_firstPoFile "${_firstPoFileArg}")

	set (_gmoFiles)
	get_filename_component (_potBasename ${_potFile} NAME_WE)
	get_filename_component (_absPotFile ${_potFile} ABSOLUTE)

	set (_addToAll)
	if (${_firstPoFile} STREQUAL "ALL")
		set (_addToAll "ALL")
		set (_firstPoFile)
	endif (${_firstPoFile} STREQUAL "ALL")

	foreach (_currentPoFile ${_firstPoFile} ${ARGN})
		get_filename_component (_absFile ${_currentPoFile} ABSOLUTE)
		get_filename_component (_abs_PATH ${_absFile} PATH)
		get_filename_component (_lang ${_absFile} NAME_WE)
		set (_gmoFile ${CMAKE_CURRENT_BINARY_DIR}/${_lang}.gmo)

		# msgmerge versions older than 0.11 don't actually support --update
		# and --backup, let's try to workaround that (tested on 0.10.40).
		execute_process (COMMAND ${GETTEXT_MSGMERGE_EXECUTABLE} -V
			OUTPUT_VARIABLE _msgmergeVersion)
		string (REGEX MATCH "0[.][0-9]+" _msgmergeVersion ${_msgmergeVersion})
		if ("${_msgmergeVersion}" MATCHES "[.]10|[.][0-9]")
			set (_msgmergeParams --quiet -s
				${_absFile} -o ${_absFile} ${_absPotFile})
		else ("${_msgmergeVersion}" MATCHES "[.]10|[.][0-9]")
			set (_msgmergeParams --quiet --update --backup=none -s
				${_absFile} ${_absPotFile})
		endif ("${_msgmergeVersion}" MATCHES "[.]10|[.][0-9]")

		add_custom_command (
			OUTPUT ${_gmoFile}
			COMMAND ${GETTEXT_MSGMERGE_EXECUTABLE} ${_msgmergeParams}
			COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -o ${_gmoFile} ${_absFile}
			DEPENDS ${_absPotFile} ${_absFile}
		)

		install (FILES ${_gmoFile} DESTINATION
			share/locale/${_lang}/LC_MESSAGES RENAME ${_potBasename}.mo)
		set (_gmoFiles ${_gmoFiles} ${_gmoFile})
	endforeach (_currentPoFile)

	add_custom_target (${PROJECT_NAME}_translations ${_addToAll}
		DEPENDS ${_gmoFiles})
endmacro (GETTEXT_CREATE_TRANSLATIONS)

if (GETTEXT_MSGMERGE_EXECUTABLE AND GETTEXT_MSGFMT_EXECUTABLE)
	set (GETTEXT_FOUND TRUE)
else (GETTEXT_MSGMERGE_EXECUTABLE AND GETTEXT_MSGFMT_EXECUTABLE)
	set (GETTEXT_FOUND FALSE)
	if (GetText_REQUIRED)
		message (FATAL_ERROR "GetText not found")
	endif (GetText_REQUIRED)
endif (GETTEXT_MSGMERGE_EXECUTABLE AND GETTEXT_MSGFMT_EXECUTABLE)


