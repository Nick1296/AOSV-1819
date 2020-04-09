.phony: source docs

all: source docs

# build module, library and the userspace program
source:
		$(MAKE) -C src/

# build the documentation in LaTeX and html
docs: FORCE
		doxygen docs/doxygen/Doxyfile
		$(MAKE) -C docs/doxygen/latex/

clean:
		$(MAKE) -C docs/doxygen/latex clean
		$(MAKE) -C src clean
# this target is used to alwys trigger the docs target,
FORCE:
