// Smooth lines from grid edges: the edge of an area found square by square
// (marching squares) comes out as a staircase; this joins its pieces into
// lines and relaxes them into curves. Used for the walk area
// (TMBattleDirectorIndicators.cpp) and the edge of sight (TMBattleDirectorFog.cpp).

#pragma once

#include "CoreMinimal.h"

namespace TMLines
{
	/**
	 * The pieces of an edge joined end to end into lines -- closed loops where
	 * the edge goes all the way round. Closed says which are loops.
	 */
	inline TArray<TArray<FVector2D>> Chain(const TArray<TPair<FVector2D, FVector2D>>& Edges, TArray<bool>& Closed)
	{
		auto Key = [](const FVector2D& P) { return FIntPoint(FMath::RoundToInt(P.X * 8.0), FMath::RoundToInt(P.Y * 8.0)); };
		TMultiMap<FIntPoint, int32> Ends;
		for (int32 i = 0; i < Edges.Num(); ++i)
		{
			Ends.Add(Key(Edges[i].Key), i);
			Ends.Add(Key(Edges[i].Value), i);
		}
		TArray<bool> Used;
		Used.Init(false, Edges.Num());
		TArray<TArray<FVector2D>> Lines;
		auto NextFrom = [&](const FVector2D& At) -> int32
		{
			TArray<int32> Found;
			Ends.MultiFind(Key(At), Found);
			for (const int32 k : Found)
			{
				if (!Used[k])
				{
					return k;
				}
			}
			return INDEX_NONE;
		};
		for (int32 Start = 0; Start < Edges.Num(); ++Start)
		{
			if (Used[Start])
			{
				continue;
			}
			Used[Start] = true;
			TArray<FVector2D> Line = { Edges[Start].Key, Edges[Start].Value };
			// Onward from the end, then back from the start.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (;;)
				{
					const FVector2D Tip = Pass == 0 ? Line.Last() : Line[0];
					const int32 k = NextFrom(Tip);
					if (k == INDEX_NONE)
					{
						break;
					}
					Used[k] = true;
					const FVector2D Other = Key(Edges[k].Key) == Key(Tip) ? Edges[k].Value : Edges[k].Key;
					if (Pass == 0)
					{
						Line.Add(Other);
					}
					else
					{
						Line.Insert(Other, 0);
					}
				}
			}
			const bool bLoop = Line.Num() > 3 && Key(Line[0]) == Key(Line.Last());
			if (bLoop)
			{
				Line.Pop();
			}
			Closed.Add(bLoop);
			Lines.Add(MoveTemp(Line));
		}
		return Lines;
	}

	/**
	 * A line relaxed into a curve: each point eased towards its neighbours
	 * (Relax times, which irons out the staircase over a few squares), then its
	 * corners cut off (Cut times, Chaikin), so it is round rather than faceted.
	 * An open line keeps its ends.
	 */
	inline TArray<FVector2D> Smooth(const TArray<FVector2D>& Line, bool bClosed, int32 Relax, int32 Cut)
	{
		TArray<FVector2D> Out = Line;
		const int32 N = Out.Num();
		for (int32 r = 0; r < Relax && N >= 3; ++r)
		{
			const TArray<FVector2D> Before = Out;
			for (int32 i = 0; i < N; ++i)
			{
				if (!bClosed && (i == 0 || i == N - 1))
				{
					continue;
				}
				const FVector2D& Prev = Before[(i + N - 1) % N];
				const FVector2D& Next = Before[(i + 1) % N];
				Out[i] = Before[i] * 0.5 + (Prev + Next) * 0.25;
			}
		}
		for (int32 t = 0; t < Cut && Out.Num() >= 3; ++t)
		{
			TArray<FVector2D> Next;
			const int32 M = Out.Num();
			const int32 Last = bClosed ? M : M - 1;
			if (!bClosed)
			{
				Next.Add(Out[0]);
			}
			for (int32 i = 0; i < Last; ++i)
			{
				const FVector2D& A = Out[i];
				const FVector2D& B = Out[(i + 1) % M];
				Next.Add(A * 0.75 + B * 0.25);
				Next.Add(A * 0.25 + B * 0.75);
			}
			if (!bClosed)
			{
				Next.Add(Out.Last());
			}
			Out = MoveTemp(Next);
		}
		return Out;
	}

	/** Every segment of a set of lines, in order: loops closed. */
	inline TArray<TPair<FVector2D, FVector2D>> Segments(const TArray<TArray<FVector2D>>& Lines, const TArray<bool>& Closed)
	{
		TArray<TPair<FVector2D, FVector2D>> Out;
		for (int32 l = 0; l < Lines.Num(); ++l)
		{
			const TArray<FVector2D>& Line = Lines[l];
			const int32 N = Line.Num();
			for (int32 i = 0; i + 1 < N; ++i)
			{
				Out.Add({ Line[i], Line[i + 1] });
			}
			if (Closed[l] && N > 2)
			{
				Out.Add({ Line[N - 1], Line[0] });
			}
		}
		return Out;
	}

	/** Chain, smooth and cut back into segments: a staircase edge made a curve. */
	inline TArray<TPair<FVector2D, FVector2D>> SmoothEdges(const TArray<TPair<FVector2D, FVector2D>>& Edges, int32 Relax, int32 Cut)
	{
		TArray<bool> Closed;
		TArray<TArray<FVector2D>> Lines = Chain(Edges, Closed);
		for (int32 l = 0; l < Lines.Num(); ++l)
		{
			Lines[l] = Smooth(Lines[l], Closed[l], Relax, Cut);
		}
		return Segments(Lines, Closed);
	}
}
