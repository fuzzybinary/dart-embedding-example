library mylib;

import 'dart:io';

void simplePrint(String s) native "SimplePrint";

void main() {
  while (true) {
    simplePrint("hello world\n");
    sleep(Duration(seconds: 3));
  }
}
