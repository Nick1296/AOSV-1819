.phony: source doc

all: source doc

source:
		$(MAKE) -C src/

doc: source
		doxygen docs/doxygen/Doxyfile
		$(MAKE) -C docs/doxygen/latex/

