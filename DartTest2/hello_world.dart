library mylib;

void simplePrint(String s) native "SimplePrint";

void main() {
  simplePrint("hello world");
}