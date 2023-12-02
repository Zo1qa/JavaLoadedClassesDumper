#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <process.h>
#include <jni.h>
#include <jvmti.h>
#include "classes.h"
#include "MinHook.h"
#include <string>
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

FILE* file = nullptr;

typedef jint (JNICALL* tGetCreatedJavaVMs)(JavaVM**, jsize, jsize*);
tGetCreatedJavaVMs GetCreatedJavaVMs = reinterpret_cast<tGetCreatedJavaVMs>(GetProcAddress(GetModuleHandle("jvm.dll"), "JNI_GetCreatedJavaVMs"));
jvmtiEnv* tiEnv = nullptr;

std::string jstring2string(JNIEnv* env, jstring jStr) {
	if (!jStr)
		return "";

	const jclass stringClass = env->GetObjectClass(jStr);
	const jmethodID getBytes = env->GetMethodID(stringClass, "getBytes", "(Ljava/lang/String;)[B");
	const jbyteArray stringJbytes = (jbyteArray)env->CallObjectMethod(jStr, getBytes, env->NewStringUTF("UTF-8"));

	size_t length = (size_t)env->GetArrayLength(stringJbytes);
	jbyte* pBytes = env->GetByteArrayElements(stringJbytes, NULL);

	std::string ret = std::string((char*)pBytes, length);
	env->ReleaseByteArrayElements(stringJbytes, pBytes, JNI_ABORT);

	env->DeleteLocalRef(stringJbytes);
	env->DeleteLocalRef(stringClass);
	return ret;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}
}

jstring StringToJString(JNIEnv* env, const std::string& nativeString) {
	return env->NewStringUTF(nativeString.c_str());
}

unsigned __stdcall Rustex(void*)
{

	jsize nVms;
	GetCreatedJavaVMs(0, 0, &nVms);

	JavaVM** jvm = new JavaVM * [nVms];
	GetCreatedJavaVMs(jvm, nVms, &nVms);

	JNIEnv* env;
	

	jvm[0]->AttachCurrentThread((void**)&env, NULL);

	jvm[0]->GetEnv((void**)&env, JNI_VERSION_1_8);
	jvm[0]->GetEnv((void**)&tiEnv, JVMTI_VERSION_1_2);
	
	jclass lang = env->FindClass("java/lang/Class");
	jclass classLoader = env->FindClass("java/lang/ClassLoader");
	jclass IOutils = env->FindClass("org/apache/commons/io/IOUtils");

	jmethodID getName = env->GetMethodID(lang, "getName", "()Ljava/lang/String;");

	jclass* classes;
	jint amount;

	tiEnv->GetLoadedClasses(&amount, &classes);


	fs::create_directories("./ClassDumper");
	for (int i = 0; i < amount; i++) {
		jstring name = (jstring)env->CallObjectMethod(classes[i], getName);
		std::string nameStr = jstring2string(env, name);

		jmethodID getClassLoaderMethod = env->GetMethodID(lang, "getClassLoader", "()Ljava/lang/ClassLoader;");
		jmethodID getInputStream = env->GetMethodID(classLoader, "getResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;");
		jmethodID toByteArray = env->GetStaticMethodID(IOutils, "toByteArray", "(Ljava/io/InputStream;)[B");

		jobject classLoader = env->CallObjectMethod(classes[i], getClassLoaderMethod);

		std::string nameStr2 = nameStr;

		replaceAll(nameStr2, ".", "/");
		nameStr2 = nameStr2 + ".class";
		
		jobject inputStream = env->CallObjectMethod(classLoader, getInputStream, StringToJString(env, nameStr2));
		jobject bytesClass = env->CallObjectMethod(IOutils, toByteArray, inputStream);
		if (bytesClass == NULL) {
			std::cout << "zero class " << nameStr2 << std::endl;
			continue;
		}

		jbyteArray bytesClass2 = (jbyteArray)bytesClass;

		jsize len = env->GetArrayLength(bytesClass2);
		jbyte* body = env->GetByteArrayElements(bytesClass2, 0);

		

		nameStr = "./ClassDumper/" + nameStr + ".class";

		FILE* out = fopen(nameStr.c_str(), "wb");
		fwrite(body, 1, len, out);
		fclose(out);

		env->ReleaseByteArrayElements(bytesClass2, body, JNI_ABORT);
		std::cout << nameStr << " " << std::endl;
	}


	jvm[0]->DetachCurrentThread();

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == 1)
	{
		DisableThreadLibraryCalls(hModule);

		AllocConsole();
		freopen_s(&file, "CONOUT$", "w", stdout);

		const auto handle = _beginthreadex(0, 0, &Rustex, 0, 0, 0);

		if (handle)
		{
			CloseHandle(reinterpret_cast<HANDLE>(handle));
		}

		return 1;
	}
	return 0;
}