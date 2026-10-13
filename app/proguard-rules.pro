# FM1 DECK R8 rules.
# JNI: native methods and the classes/fields native code looks up by name must survive shrinking.
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}
-keep class io.github.fm1deck.engine.NativeBridge { *; }
