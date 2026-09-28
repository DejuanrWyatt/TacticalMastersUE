// Godot's own sort, so that a sort in the rules puts things in the order Godot
// puts them.
//
// The computer player sorts its options and takes the first (ai_player.gd:151).
// Godot sorts with an introsort (core/templates/sort_array.h, SortArray), which
// is not stable: two options that score the same can come out in either order.
// But "not stable" is not "random". Given the same options in the same order it
// always gives the same answer, so a transcription of the algorithm itself --
// the median-of-three pivot, the depth limit, the heapsort fallback and the
// final insertion sort over runs of sixteen -- settles every tie exactly as
// Godot does. std::sort is also an introsort, but not this one, and would not.
//
// Compare(A, B) means "A goes before B", as a GDScript sort_custom callable
// does. Godot sorts an Array with its validating SortArray, which stops a scan
// that runs off the end when the comparison is not a strict ordering; for a
// strict one (the only kind the rules use) it never fires, so it is left out.

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace TMSim
{
	template <typename T, typename Compare>
	class TGodotSort
	{
	public:
		explicit TGodotSort(Compare InCompare) : Less(InCompare) {}

		void Sort(std::vector<T>& Items)
		{
			const int64_t Len = static_cast<int64_t>(Items.size());
			if (Len != 0)
			{
				Introsort(0, Len, Items.data(), BitLog(Len) * 2);
				FinalInsertionSort(0, Len, Items.data());
			}
		}

	private:
		static constexpr int64_t IntrosortThreshold = 16;
		Compare Less;

		const T& MedianOf3(const T& A, const T& B, const T& C) const
		{
			if (Less(A, B))
			{
				if (Less(B, C))
				{
					return B;
				}
				if (Less(A, C))
				{
					return C;
				}
				return A;
			}
			if (Less(A, C))
			{
				return A;
			}
			if (Less(B, C))
			{
				return C;
			}
			return B;
		}

		static int64_t BitLog(int64_t N)
		{
			int64_t K = 0;
			for (; N != 1; N >>= 1)
			{
				++K;
			}
			return K;
		}

		// ------------------------------------------------------------- heaps

		void PushHeap(int64_t First, int64_t Hole, int64_t Top, T Value, T* Array) const
		{
			int64_t Parent = (Hole - 1) / 2;
			while (Hole > Top && Less(Array[First + Parent], Value))
			{
				Array[First + Hole] = Array[First + Parent];
				Hole = Parent;
				Parent = (Hole - 1) / 2;
			}
			Array[First + Hole] = Value;
		}

		void AdjustHeap(int64_t First, int64_t Hole, int64_t Len, T Value, T* Array) const
		{
			const int64_t Top = Hole;
			int64_t Second = 2 * Hole + 2;
			while (Second < Len)
			{
				if (Less(Array[First + Second], Array[First + (Second - 1)]))
				{
					--Second;
				}
				Array[First + Hole] = Array[First + Second];
				Hole = Second;
				Second = 2 * (Second + 1);
			}
			if (Second == Len)
			{
				Array[First + Hole] = Array[First + (Second - 1)];
				Hole = Second - 1;
			}
			PushHeap(First, Hole, Top, Value, Array);
		}

		void PopHeap(int64_t First, int64_t Last, int64_t Result, T Value, T* Array) const
		{
			Array[Result] = Array[First];
			AdjustHeap(First, 0, Last - First, Value, Array);
		}

		void PopHeap(int64_t First, int64_t Last, T* Array) const
		{
			PopHeap(First, Last - 1, Last - 1, Array[Last - 1], Array);
		}

		void SortHeap(int64_t First, int64_t Last, T* Array) const
		{
			while (Last - First > 1)
			{
				PopHeap(First, Last--, Array);
			}
		}

		void MakeHeap(int64_t First, int64_t Last, T* Array) const
		{
			if (Last - First < 2)
			{
				return;
			}
			const int64_t Len = Last - First;
			int64_t Parent = (Len - 2) / 2;
			while (true)
			{
				AdjustHeap(First, Parent, Len, Array[First + Parent], Array);
				if (Parent == 0)
				{
					return;
				}
				--Parent;
			}
		}

		void PartialSort(int64_t First, int64_t Last, int64_t Middle, T* Array) const
		{
			MakeHeap(First, Middle, Array);
			for (int64_t i = Middle; i < Last; ++i)
			{
				if (Less(Array[i], Array[First]))
				{
					PopHeap(First, Middle, i, Array[i], Array);
				}
			}
			SortHeap(First, Middle, Array);
		}

		// ------------------------------------------------------- introsort

		int64_t Partitioner(int64_t First, int64_t Last, T Pivot, T* Array) const
		{
			while (true)
			{
				while (Less(Array[First], Pivot))
				{
					++First;
				}
				--Last;
				while (Less(Pivot, Array[Last]))
				{
					--Last;
				}
				if (!(First < Last))
				{
					return First;
				}
				std::swap(Array[First], Array[Last]);
				++First;
			}
		}

		void Introsort(int64_t First, int64_t Last, T* Array, int64_t MaxDepth) const
		{
			while (Last - First > IntrosortThreshold)
			{
				if (MaxDepth == 0)
				{
					PartialSort(First, Last, Last, Array);
					return;
				}
				--MaxDepth;
				// The pivot is copied, as Godot passes it by value.
				const int64_t Cut = Partitioner(First, Last,
					MedianOf3(Array[First], Array[First + (Last - First) / 2], Array[Last - 1]), Array);
				Introsort(Cut, Last, Array, MaxDepth);
				Last = Cut;
			}
		}

		// -------------------------------------------------- insertion sort

		void UnguardedLinearInsert(int64_t Last, T Value, T* Array) const
		{
			int64_t Next = Last - 1;
			while (Less(Value, Array[Next]))
			{
				Array[Last] = Array[Next];
				Last = Next;
				--Next;
			}
			Array[Last] = Value;
		}

		void LinearInsert(int64_t First, int64_t Last, T* Array) const
		{
			T Value = Array[Last];
			if (Less(Value, Array[First]))
			{
				for (int64_t i = Last; i > First; --i)
				{
					Array[i] = Array[i - 1];
				}
				Array[First] = Value;
			}
			else
			{
				UnguardedLinearInsert(Last, Value, Array);
			}
		}

		void InsertionSort(int64_t First, int64_t Last, T* Array) const
		{
			if (First == Last)
			{
				return;
			}
			for (int64_t i = First + 1; i != Last; ++i)
			{
				LinearInsert(First, i, Array);
			}
		}

		void UnguardedInsertionSort(int64_t First, int64_t Last, T* Array) const
		{
			for (int64_t i = First; i != Last; ++i)
			{
				UnguardedLinearInsert(i, Array[i], Array);
			}
		}

		void FinalInsertionSort(int64_t First, int64_t Last, T* Array) const
		{
			if (Last - First > IntrosortThreshold)
			{
				InsertionSort(First, First + IntrosortThreshold, Array);
				UnguardedInsertionSort(First + IntrosortThreshold, Last, Array);
			}
			else
			{
				InsertionSort(First, Last, Array);
			}
		}
	};

	/** Sorts as Godot's Array.sort_custom does, ties and all. Compare(A, B): A goes before B. */
	template <typename T, typename Compare>
	void GodotSort(std::vector<T>& Items, Compare InCompare)
	{
		TGodotSort<T, Compare>(InCompare).Sort(Items);
	}
}
