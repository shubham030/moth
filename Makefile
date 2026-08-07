# moth — common tasks. Run `make help` for the list.
RUBY_BIN := /opt/homebrew/opt/ruby/bin
MOTHC := dart run tools/mothc/bin/mothc.dart
MOTHRUN := ./vm/build/mothrun

.PHONY: help vm deps test blink sim docs docs-build render clean

help:
	@echo "make vm        build the VM and the mothrun simulator"
	@echo "make test      run the golden test suite"
	@echo "make blink     compile and run examples/blink.dart with no hardware"
	@echo "make sim F=x   compile and run any .dart file in the simulator"
	@echo "make docs      serve the documentation site at http://127.0.0.1:4111/moth/"
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
	cd docs && PATH="$(RUBY_BIN):$$PATH" bundle exec jekyll serve --port 4111 --host 127.0.0.1

docs-build:
	cd docs && PATH="$(RUBY_BIN):$$PATH" bundle exec jekyll build

render:
	cmake -B moth_render/build moth_render && cmake --build moth_render/build

clean:
	rm -rf vm/build moth_render/build docs/_site docs/.jekyll-cache
	rm -f examples/*.mothb test/cases/*.mothb
