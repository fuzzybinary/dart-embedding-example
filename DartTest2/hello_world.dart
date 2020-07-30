library mylib;

import 'package:vector_math/vector_math.dart';
import 'package:dart_test/natives.dart';

void main() {
  simplePrint("hello world");

  var vec = Vector2.all(1.0);
  simplePrint("Value of vector i $vec");
}
