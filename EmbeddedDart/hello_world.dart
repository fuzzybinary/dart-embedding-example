import 'dart:io';

void simplePrint(String s) native "SimplePrint";

void main() {
  simplePrint("hello world\n");
  sleep(Duration(seconds: 3));
}
