library mylib;

void simplePrint(String s) native "SimplePrint";

void main() {
  simplePrint("hello world");

  int loop = 0;
  while(true) {
    loop++;
  }
  
}