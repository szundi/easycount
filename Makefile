.PHONY: all up down

SOURCES := $(shell find src resources -type f) wscript scripts/build-variant.sh

all: up down

up: dist/easycount-1.pbw

down: dist/easycount-0.pbw

node_modules/@rebble/clay:
	npm install

dist/easycount-1.pbw: $(SOURCES) variants/up/package.json node_modules/@rebble/clay
	./scripts/build-variant.sh variants/up/package.json

dist/easycount-0.pbw: $(SOURCES) variants/down/package.json node_modules/@rebble/clay
	./scripts/build-variant.sh variants/down/package.json
