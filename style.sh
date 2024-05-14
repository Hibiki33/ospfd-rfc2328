#!/bin/bash

set -e

# clang-format
[ -z "$CLANG_FORMAT" ] && CLANG_FORMAT=clang-format

fix="no"
opt="escp"

while [[ $# -gt 0 ]]
do
	case $1 in
	-f|--fix)
		fix="yes"
		shift
		;;
	-c|--check)
		if [ $# -lt 2 ]; then
			echo "Expect spec check following -c/--check (choose from 'escp')"
			echo "  e  editorconfig check"
			echo "  s  shellscript check"
			echo "  c  clang-format check/format"
			echo "  p  python-pep8 check/format"
			exit 1
		fi
		shift
		opt="$1"
		if [[ "$opt" =~ [^escp] ]]; then
			echo "Unknown spec check $opt"
			exit 1
		fi
		shift
		;;
	*)
		echo "Unknown option $1"
		exit 1
		;;
	esac
done

function green() {
	echo -e "\033[0;32m$1\033[0m"
}

function warning() {
	echo -e "\033[0;33m$1\033[0m"
}

function fatal() {
	echo -e "\033[0;31m$1\033[0m"
}

function check-cl() {
	green '[ clang-format ] fix begin'
	# Use '-follow' in case 'quizs' is symlinked.
	cpp_files=$(find -- * -regextype sed -regex '.*\.[ch]\(pp\)\?' -a -print -follow)

	if [ "$fix" = "yes" ]; then
		# shellcheck disable=2086
		if [ -n "$cpp_files" ]; then
			if ! $CLANG_FORMAT -i $cpp_files ; then
			warning '[ clang-format ] maybe version too low?'
		fi
		else
			warning "No C++ files found for formatting."
		fi
		green '[ clang-format ] fix end'
		return 0
	else
		# shellcheck disable=2086
		cnt=$($CLANG_FORMAT $cpp_files --output-replacements-xml | grep -c "<replacement "; exit "${PIPESTATUS[0]}")
		if [ "${PIPESTATUS[0]}" -ne 0 ] && [ "$cnt" -eq 0 ]; then
			warning '[ clang-format ] maybe version too low?'
		fi
		if [ "$cnt" -eq 0 ]; then
			green '[ clang-format ] check passed'
			return 0
		else
			fatal "[ clang-format ] check failed"
			return 1
		fi
	fi
}

r=0
if [[ "$opt" == *"c"* ]] && ! check-cl; then
	r=3
fi
exit $r
