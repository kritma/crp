cd "$(dirname "$0")"
mkdir -p build
clang -w ../crp.c -o build/crp
./build/crp -c crp.conf build/assets.o -q
clang src/hello_world.c build/assets.o -o hello_world
./hello_world
