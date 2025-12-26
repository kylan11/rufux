SHELL := bash
.DEFAULT_GOAL := build

.PHONY: build

build:
	meson compile -C ./build
	cp ./build/rufux ~/.local/bin/rufux
