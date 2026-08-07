# moth — common tasks. Run `make help` for the list.
MOTHC := dart run tools/mothc/bin/mothc.dart
MOTHRUN := ./vm/build/mothrun

.PHONY: help vm deps test blink sim docs docs-build render clean

help:
	@echo "make vm        build the VM and the mothrun simulator"
	@echo "make test      run the golden test suite"
	@echo "make blink     compile and run examples/blink.dart with no hardware"
	@echo "make sim F=x   compile and run any .dart file in the simulator"
	@echo "make docs      serve the documentation site (Docusaurus dev server)"
	@echo "make render    build moth_render and its SDL harness"
	@echo "make clean     remove build outputs"

vm:
	cmake -B vm/build vm && cmake --build vm/build

deps:
	dart pub get --directory tools/mothc

test: vm deps
	cd tools/mothc && dart test

blink: vm
	$(MOTHC) examples/blink.dart
	$(MOTHRUN) examples/blink.mothb --stop-after 3000

# make sim F=examples/button.dart
sim: vm
	$(MOTHC) $(F)
	$(MOTHRUN) $(basename $(F)).mothb --stop-after 2000

docs:
	cd website && npm install && npm start

docs-build:
	cd website && npm install && npm run build

render:
	cmake -B moth_render/build moth_render && cmake --build moth_render/build

clean:
	rm -rf vm/build moth_render/build website/build website/.docusaurus
	rm -f examples/*.mothb test/cases/*.mothb
