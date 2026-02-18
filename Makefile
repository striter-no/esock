all:
	gcc -g -o ./bin/main ./code/main.c -Icode/esocks/include -Lcode/esocks/lib
run:
	./bin/main
clean:
	rm -rf ./bin/*
