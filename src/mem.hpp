#pragma once

#include <stdlib.h>
#include <stdint.h>

template <typename T>
struct remove_const
{
	using type = T;
};

template <typename T>
struct remove_const<T const>
{
	using type = T;
};

template <typename T>
T* tmalloc(uint32_t count = 1)
{
	return reinterpret_cast<T*>(malloc(sizeof(T) * count));
}

template <typename T>
T* trealloc(T* ptr, uint32_t count = 1)
{
	return reinterpret_cast<T*>(realloc(ptr, sizeof(T) * count));
}

template <typename T>
void tfree(T* ptr)
{
	auto unconst_ptr = const_cast<remove_const<T>::type*>(ptr);

	free(unconst_ptr);
}