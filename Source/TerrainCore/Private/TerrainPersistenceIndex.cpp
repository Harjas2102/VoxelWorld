// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainPersistenceIndex.h"

#include "Algo/Sort.h"

/**
 * The 96-bit chunk-key radix index (Docs/proposals/P-004 section 6).
 *
 * PERMANENT FORMAT for the page body. The tree SHAPE is also permanent in a subtler way: a
 * page's identity is the digest of its own bytes, and its bytes include its depth and key
 * prefix, so two structurally different trees over the same keys have different root digests.
 * Changing how pages are built therefore changes every root digest ever published.
 */

namespace
{
	/**
	 * Sign-flip then BIG-ENDIAN. Both halves are load-bearing.
	 *
	 * Big-endian makes the most significant byte the first one a radix walk consumes, and the
	 * sign flip maps INT32_MIN..INT32_MAX onto 0..UINT32_MAX in order. Together they make
	 * unsigned byte-wise comparison of the 12-byte key agree exactly with signed comparison of
	 * (X, Y, Z) -- which is the property the whole sorted-page structure rests on.
	 */
	void WriteCoordinate(uint8* Out, int32 Value)
	{
		const uint32 Unsigned = static_cast<uint32>(Value) ^ 0x80000000u;
		Out[0] = static_cast<uint8>((Unsigned >> 24) & 0xFFu);
		Out[1] = static_cast<uint8>((Unsigned >> 16) & 0xFFu);
		Out[2] = static_cast<uint8>((Unsigned >>  8) & 0xFFu);
		Out[3] = static_cast<uint8>( Unsigned        & 0xFFu);
	}

	int32 ReadCoordinate(const uint8* In)
	{
		const uint32 Unsigned =
			  (static_cast<uint32>(In[0]) << 24)
			| (static_cast<uint32>(In[1]) << 16)
			| (static_cast<uint32>(In[2]) <<  8)
			|  static_cast<uint32>(In[3]);
		return static_cast<int32>(Unsigned ^ 0x80000000u);
	}

	/** An update paired with its index key, sorted once and then walked by range. */
	struct FSortedUpdate
	{
		FTerrainIndexKey       Key;
		FTerrainIndexLeafValue Value;
	};

	bool IsLeafValueConsistent(const FTerrainIndexLeafValue& Value)
	{
		if (Value.Encoding == ETerrainRegionEncoding::Empty)
		{
			return Value.PayloadLength == 0 && Value.PayloadDigest.IsZero();
		}
		return Value.PayloadLength > 0 && !Value.PayloadDigest.IsZero();
	}

	ETerrainPersistError LoadPage(
		const FTerrainPersistIdentity& Identity,
		const ITerrainObjectStore& Source,
		const FTerrainDigest& Digest,
		uint32 ExpectedLength,
		int32 ExpectedDepth,
		const uint8* ExpectedPrefix,
		FTerrainIndexPage& OutPage)
	{
		TArray<uint8> Bytes;
		if (!Source.LoadObject(Digest, Bytes))
		{
			// A referenced object that is not there. P-003 section 3 is explicit that a missing
			// named dependency is corruption, never "nothing more to read".
			return ETerrainPersistError::ShortBuffer;
		}
		if (static_cast<uint32>(Bytes.Num()) != ExpectedLength)
		{
			return ETerrainPersistError::BodyLengthOutOfRange;
		}
		if (TerrainPersistDigest(Bytes) != Digest)
		{
			return ETerrainPersistError::BodyChecksumMismatch;
		}

		FTerrainPersistObjectHeader Header;
		TArrayView<const uint8> Body;
		const ETerrainPersistError ObjectError = TerrainPersistDecodeObject(
			Bytes, ETerrainPersistObjectType::IndexPage, &Identity, Header, Body);
		if (ObjectError != ETerrainPersistError::None)
		{
			return ObjectError;
		}

		const ETerrainPersistError PageError = TerrainIndexDecodePageBody(Body, OutPage);
		if (PageError != ETerrainPersistError::None)
		{
			return PageError;
		}

		// Depth and prefix are checked against the traversal, not merely against themselves:
		// this is what makes a cross-prefix reference impossible to follow (P-004 section 6.3).
		if (OutPage.Depth != ExpectedDepth)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		if (FMemory::Memcmp(OutPage.KeyPrefix, ExpectedPrefix, TerrainPersistIndexKeyBytes) != 0)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		return ETerrainPersistError::None;
	}

	ETerrainPersistError StorePage(
		const FTerrainPersistIdentity& Identity,
		ITerrainObjectStore& Sink,
		const FTerrainIndexPage& Page,
		FTerrainDigest& OutDigest,
		uint32& OutLength)
	{
		TArray<uint8> Body;
		const ETerrainPersistError BodyError = TerrainIndexEncodePageBody(Page, Body);
		if (BodyError != ETerrainPersistError::None)
		{
			return BodyError;
		}

		TArray<uint8> Object;
		const ETerrainPersistError ObjectError =
			TerrainPersistEncodeObject(ETerrainPersistObjectType::IndexPage, Identity, Body, Object);
		if (ObjectError != ETerrainPersistError::None)
		{
			return ObjectError;
		}

		OutDigest = TerrainPersistDigest(Object);
		OutLength = static_cast<uint32>(Object.Num());

		if (!Sink.StoreObject(OutDigest, Object))
		{
			return ETerrainPersistError::BodyChecksumMismatch;
		}
		return ETerrainPersistError::None;
	}

	struct FApplyContext
	{
		const FTerrainPersistIdentity& Identity;
		const ITerrainObjectStore&     Source;
		ITerrainObjectStore&           Sink;
		int32                          PagesWritten = 0;
	};

	/**
	 * Rewrites the subtree at Depth covering Updates[Begin, End), returning the new child ref.
	 *
	 * Every page on the path to a changed leaf is written as a new immutable object; every
	 * sibling entry that no update touched is carried over by the digest it already had, which
	 * is what makes the work proportional to changed keys and not to history.
	 */
	ETerrainPersistError ApplySubtree(
		FApplyContext& Context,
		int32 Depth,
		const uint8* Prefix,
		const FTerrainDigest* OldDigest,
		uint32 OldLength,
		const TArray<FSortedUpdate>& Updates,
		int32 Begin,
		int32 End,
		FTerrainDigest& OutDigest,
		uint32& OutLength)
	{
		check(Begin < End);

		FTerrainIndexPage Page;
		Page.Depth = static_cast<uint8>(Depth);
		Page.bLeaf = (Depth == TerrainIndexLeafDepth);
		FMemory::Memcpy(Page.KeyPrefix, Prefix, TerrainPersistIndexKeyBytes);

		FTerrainIndexPage OldPage;
		bool bHasOldPage = false;
		if (OldDigest != nullptr)
		{
			const ETerrainPersistError LoadError = LoadPage(
				Context.Identity, Context.Source, *OldDigest, OldLength, Depth, Prefix, OldPage);
			if (LoadError != ETerrainPersistError::None)
			{
				return LoadError;
			}
			bHasOldPage = true;
		}

		if (Page.bLeaf)
		{
			// Merge: every old leaf entry survives unless an update replaces it. There is no
			// delete -- an edited chunk that matches the base again becomes Empty, not absent,
			// because absent means "never touched, regenerate" and that is a different fact.
			int32 OldIndex = 0;
			int32 UpdateIndex = Begin;

			while (UpdateIndex < End || (bHasOldPage && OldIndex < OldPage.Leaves.Num()))
			{
				const bool bHaveOld    = bHasOldPage && OldIndex < OldPage.Leaves.Num();
				const bool bHaveUpdate = UpdateIndex < End;

				const uint8 OldByte    = bHaveOld    ? OldPage.Leaves[OldIndex].ByteValue : 0;
				const uint8 UpdateByte = bHaveUpdate ? Updates[UpdateIndex].Key.Bytes[Depth] : 0;

				if (bHaveOld && (!bHaveUpdate || OldByte < UpdateByte))
				{
					Page.Leaves.Add(OldPage.Leaves[OldIndex++]);
					continue;
				}

				FTerrainIndexLeafEntry Entry;
				Entry.ByteValue = UpdateByte;
				Entry.Value     = Updates[UpdateIndex].Value;
				Page.Leaves.Add(Entry);

				if (bHaveOld && OldByte == UpdateByte)
				{
					++OldIndex;   // replaced
				}
				++UpdateIndex;
			}

			if (Page.Leaves.Num() > TerrainPersistMaxIndexEntriesPerPage)
			{
				return ETerrainPersistError::CapExceeded;
			}
		}
		else
		{
			int32 OldIndex = 0;
			int32 Cursor = Begin;

			while (Cursor < End || (bHasOldPage && OldIndex < OldPage.Internal.Num()))
			{
				const bool bHaveOld    = bHasOldPage && OldIndex < OldPage.Internal.Num();
				const bool bHaveUpdate = Cursor < End;

				const uint8 OldByte    = bHaveOld    ? OldPage.Internal[OldIndex].ByteValue : 0;
				const uint8 UpdateByte = bHaveUpdate ? Updates[Cursor].Key.Bytes[Depth] : 0;

				if (bHaveOld && (!bHaveUpdate || OldByte < UpdateByte))
				{
					Page.Internal.Add(OldPage.Internal[OldIndex++]);   // cold subtree, shared by digest
					continue;
				}

				// The contiguous run of updates that share this byte at this depth.
				int32 RunEnd = Cursor;
				while (RunEnd < End && Updates[RunEnd].Key.Bytes[Depth] == UpdateByte)
				{
					++RunEnd;
				}

				uint8 ChildPrefix[TerrainPersistIndexKeyBytes];
				FMemory::Memcpy(ChildPrefix, Prefix, TerrainPersistIndexKeyBytes);
				ChildPrefix[Depth] = UpdateByte;

				const FTerrainDigest* ChildOldDigest = nullptr;
				uint32 ChildOldLength = 0;
				if (bHaveOld && OldByte == UpdateByte)
				{
					ChildOldDigest = &OldPage.Internal[OldIndex].ChildDigest;
					ChildOldLength = OldPage.Internal[OldIndex].ChildLength;
				}

				FTerrainIndexInternalEntry Entry;
				Entry.ByteValue = UpdateByte;

				const ETerrainPersistError ChildError = ApplySubtree(
					Context, Depth + 1, ChildPrefix, ChildOldDigest, ChildOldLength,
					Updates, Cursor, RunEnd, Entry.ChildDigest, Entry.ChildLength);
				if (ChildError != ETerrainPersistError::None)
				{
					return ChildError;
				}

				Page.Internal.Add(Entry);

				if (bHaveOld && OldByte == UpdateByte)
				{
					++OldIndex;
				}
				Cursor = RunEnd;
			}

			if (Page.Internal.Num() > TerrainPersistMaxIndexEntriesPerPage)
			{
				return ETerrainPersistError::CapExceeded;
			}
		}

		const ETerrainPersistError StoreError =
			StorePage(Context.Identity, Context.Sink, Page, OutDigest, OutLength);
		if (StoreError != ETerrainPersistError::None)
		{
			return StoreError;
		}

		++Context.PagesWritten;
		return ETerrainPersistError::None;
	}

	ETerrainPersistError ValidateSubtree(
		const FTerrainPersistIdentity& Identity,
		const ITerrainObjectStore& Source,
		int32 Depth,
		const uint8* Prefix,
		const FTerrainDigest& Digest,
		uint32 Length,
		FTerrainIndexValidation& Stats,
		TArray<FTerrainChunkKey>* OutKeys)
	{
		FTerrainIndexPage Page;
		const ETerrainPersistError LoadError =
			LoadPage(Identity, Source, Digest, Length, Depth, Prefix, Page);
		if (LoadError != ETerrainPersistError::None)
		{
			return LoadError;
		}

		++Stats.PageCount;
		Stats.MaxDepthSeen = FMath::Max(Stats.MaxDepthSeen, Depth + 1);

		if (Page.bLeaf)
		{
			Stats.LeafCount += Page.Leaves.Num();
			for (const FTerrainIndexLeafEntry& Entry : Page.Leaves)
			{
				Stats.TotalPayloadBytes += Entry.Value.PayloadLength;
				if (OutKeys != nullptr)
				{
					FTerrainIndexKey Key;
					FMemory::Memcpy(Key.Bytes, Prefix, TerrainPersistIndexKeyBytes);
					Key.Bytes[Depth] = Entry.ByteValue;
					OutKeys->Add(TerrainChunkKeyFromIndex(Key));
				}
			}
			return ETerrainPersistError::None;
		}

		for (const FTerrainIndexInternalEntry& Entry : Page.Internal)
		{
			uint8 ChildPrefix[TerrainPersistIndexKeyBytes];
			FMemory::Memcpy(ChildPrefix, Prefix, TerrainPersistIndexKeyBytes);
			ChildPrefix[Depth] = Entry.ByteValue;

			const ETerrainPersistError ChildError = ValidateSubtree(
				Identity, Source, Depth + 1, ChildPrefix, Entry.ChildDigest, Entry.ChildLength,
				Stats, OutKeys);
			if (ChildError != ETerrainPersistError::None)
			{
				return ChildError;
			}
		}
		return ETerrainPersistError::None;
	}
}

// ---- key transform ------------------------------------------------------

FTerrainIndexKey TerrainIndexKeyFromChunk(const FTerrainChunkKey& Key)
{
	FTerrainIndexKey Out;
	WriteCoordinate(Out.Bytes + 0, Key.X);
	WriteCoordinate(Out.Bytes + 4, Key.Y);
	WriteCoordinate(Out.Bytes + 8, Key.Z);
	return Out;
}

FTerrainChunkKey TerrainChunkKeyFromIndex(const FTerrainIndexKey& Key)
{
	return FTerrainChunkKey(
		ReadCoordinate(Key.Bytes + 0),
		ReadCoordinate(Key.Bytes + 4),
		ReadCoordinate(Key.Bytes + 8));
}

// ---- page codec ---------------------------------------------------------

ETerrainPersistError TerrainIndexEncodePageBody(const FTerrainIndexPage& In, TArray<uint8>& OutBody)
{
	if (In.Depth > TerrainIndexLeafDepth)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (In.bLeaf != (In.Depth == TerrainIndexLeafDepth))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	const int32 EntryCount = In.bLeaf ? In.Leaves.Num() : In.Internal.Num();
	if (EntryCount == 0)
	{
		// An empty page is never published: it would be a node that says nothing while still
		// having to be read, validated and garbage-collected.
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (EntryCount > TerrainPersistMaxIndexEntriesPerPage)
	{
		return ETerrainPersistError::CapExceeded;
	}
	if ((In.bLeaf && In.Internal.Num() != 0) || (!In.bLeaf && In.Leaves.Num() != 0))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	for (int32 Index = static_cast<int32>(In.Depth); Index < TerrainPersistIndexKeyBytes; ++Index)
	{
		if (In.KeyPrefix[Index] != 0)
		{
			return ETerrainPersistError::ReservedNotZero;
		}
	}

	// Strictly ascending by byte value. This subsumes duplicate detection: two entries for the
	// same byte cannot both be reachable, and deciding which one wins is not a question a save
	// format should ever have to answer.
	for (int32 Index = 1; Index < EntryCount; ++Index)
	{
		const uint8 Previous = In.bLeaf ? In.Leaves[Index - 1].ByteValue : In.Internal[Index - 1].ByteValue;
		const uint8 Current  = In.bLeaf ? In.Leaves[Index].ByteValue     : In.Internal[Index].ByteValue;
		if (Current <= Previous)
		{
			return ETerrainPersistError::OrderViolation;
		}
	}

	if (In.bLeaf)
	{
		for (const FTerrainIndexLeafEntry& Entry : In.Leaves)
		{
			if (static_cast<uint8>(Entry.Value.Encoding) > static_cast<uint8>(ETerrainRegionEncoding::Empty))
			{
				return ETerrainPersistError::FieldOutOfRange;
			}
			if (!IsLeafValueConsistent(Entry.Value))
			{
				return ETerrainPersistError::FieldOutOfRange;
			}
			if (Entry.Value.PayloadLength >
				static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxChunkPayloadBody))
			{
				return ETerrainPersistError::CapExceeded;
			}
		}
	}
	else
	{
		for (const FTerrainIndexInternalEntry& Entry : In.Internal)
		{
			if (Entry.ChildDigest.IsZero() || Entry.ChildLength == 0
				|| Entry.ChildLength > static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxIndexPageBody))
			{
				return ETerrainPersistError::FieldOutOfRange;
			}
		}
	}

	const int32 TotalLength = TerrainIndexPageHeaderBytes
		+ EntryCount * (In.bLeaf ? TerrainIndexLeafEntryBytes : TerrainIndexInternalEntryBytes);
	if (TotalLength > TerrainPersistMaxIndexPageBody)
	{
		return ETerrainPersistError::CapExceeded;
	}

	OutBody.Reset();
	OutBody.Reserve(TotalLength);
	FTerrainByteWriter Writer(OutBody);

	Writer.WriteU8(In.Depth);                                   // 0
	Writer.WriteU8(In.bLeaf ? 1u : 0u);                         // 1
	Writer.WriteU16(static_cast<uint16>(EntryCount));           // 2 ..  3
	Writer.WriteBytes(In.KeyPrefix, TerrainPersistIndexKeyBytes); // 4 .. 15

	check(Writer.BytesWritten() == TerrainIndexPageHeaderBytes);

	if (In.bLeaf)
	{
		for (const FTerrainIndexLeafEntry& Entry : In.Leaves)
		{
			Writer.WriteU8(Entry.ByteValue);
			Writer.WriteU8(static_cast<uint8>(Entry.Value.Encoding));
			Writer.WriteU32(Entry.Value.Rev);
			Writer.WriteU64(Entry.Value.LastOpSeq);
			Writer.WriteU32(Entry.Value.PayloadLength);
			Writer.WriteDigest(Entry.Value.PayloadDigest);
		}
	}
	else
	{
		for (const FTerrainIndexInternalEntry& Entry : In.Internal)
		{
			Writer.WriteU8(Entry.ByteValue);
			Writer.WriteDigest(Entry.ChildDigest);
			Writer.WriteU32(Entry.ChildLength);
		}
	}

	check(Writer.BytesWritten() == TotalLength);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainIndexDecodePageBody(TArrayView<const uint8> Body, FTerrainIndexPage& Out)
{
	if (Body.Num() < TerrainIndexPageHeaderBytes)
	{
		return ETerrainPersistError::ShortBuffer;
	}

	FTerrainByteReader Reader(Body);
	FTerrainIndexPage Decoded;

	Decoded.Depth = Reader.ReadU8();
	const uint8 PageKind = Reader.ReadU8();
	const uint16 EntryCount = Reader.ReadU16();
	Reader.ReadBytes(Decoded.KeyPrefix, TerrainPersistIndexKeyBytes);

	if (!Reader.IsValid()) { return Reader.Error(); }

	if (Decoded.Depth > TerrainIndexLeafDepth || PageKind > 1)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	Decoded.bLeaf = PageKind == 1;
	if (Decoded.bLeaf != (Decoded.Depth == TerrainIndexLeafDepth))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (EntryCount == 0)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (EntryCount > TerrainPersistMaxIndexEntriesPerPage)
	{
		return ETerrainPersistError::CapExceeded;
	}
	for (int32 Index = static_cast<int32>(Decoded.Depth); Index < TerrainPersistIndexKeyBytes; ++Index)
	{
		if (Decoded.KeyPrefix[Index] != 0)
		{
			return ETerrainPersistError::ReservedNotZero;
		}
	}

	int32 PreviousByte = -1;
	if (Decoded.bLeaf)
	{
		Decoded.Leaves.Reserve(EntryCount);
		for (uint16 Index = 0; Index < EntryCount; ++Index)
		{
			FTerrainIndexLeafEntry Entry;
			Entry.ByteValue = Reader.ReadU8();
			const uint8 EncodingByte = Reader.ReadU8();
			Entry.Value.Rev           = Reader.ReadU32();
			Entry.Value.LastOpSeq     = Reader.ReadU64();
			Entry.Value.PayloadLength = Reader.ReadU32();
			Reader.ReadDigest(Entry.Value.PayloadDigest);
			if (!Reader.IsValid()) { return Reader.Error(); }

			if (EncodingByte > static_cast<uint8>(ETerrainRegionEncoding::Empty))
			{
				return ETerrainPersistError::FieldOutOfRange;
			}
			Entry.Value.Encoding = static_cast<ETerrainRegionEncoding>(EncodingByte);

			if (static_cast<int32>(Entry.ByteValue) <= PreviousByte)
			{
				return ETerrainPersistError::OrderViolation;
			}
			PreviousByte = static_cast<int32>(Entry.ByteValue);

			if (!IsLeafValueConsistent(Entry.Value))
			{
				return ETerrainPersistError::FieldOutOfRange;
			}
			if (Entry.Value.PayloadLength >
				static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxChunkPayloadBody))
			{
				return ETerrainPersistError::CapExceeded;
			}

			Decoded.Leaves.Add(Entry);
		}
	}
	else
	{
		Decoded.Internal.Reserve(EntryCount);
		for (uint16 Index = 0; Index < EntryCount; ++Index)
		{
			FTerrainIndexInternalEntry Entry;
			Entry.ByteValue = Reader.ReadU8();
			Reader.ReadDigest(Entry.ChildDigest);
			Entry.ChildLength = Reader.ReadU32();
			if (!Reader.IsValid()) { return Reader.Error(); }

			if (static_cast<int32>(Entry.ByteValue) <= PreviousByte)
			{
				return ETerrainPersistError::OrderViolation;
			}
			PreviousByte = static_cast<int32>(Entry.ByteValue);

			if (Entry.ChildDigest.IsZero() || Entry.ChildLength == 0
				|| Entry.ChildLength > static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxIndexPageBody))
			{
				return ETerrainPersistError::FieldOutOfRange;
			}

			Decoded.Internal.Add(Entry);
		}
	}

	if (!Reader.IsValid()) { return Reader.Error(); }
	if (!Reader.AtEnd())   { return ETerrainPersistError::TrailingBytes; }

	Out = MoveTemp(Decoded);
	return ETerrainPersistError::None;
}

// ---- the in-memory store ------------------------------------------------

bool FTerrainMemoryObjectStore::LoadObject(const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const
{
	const TArray<uint8>* Found = Objects.Find(Digest);
	if (Found == nullptr)
	{
		return false;
	}
	OutBytes = *Found;
	return true;
}

bool FTerrainMemoryObjectStore::StoreObject(const FTerrainDigest& Digest, TArrayView<const uint8> Bytes)
{
	// Content addressing is only a guarantee if it is checked on the way in as well as out.
	if (TerrainPersistDigest(Bytes) != Digest)
	{
		return false;
	}

	TArray<uint8>& Slot = Objects.FindOrAdd(Digest);
	if (Slot.Num() == 0)
	{
		Slot.Append(Bytes.GetData(), Bytes.Num());
	}
	return true;
}

int64 FTerrainMemoryObjectStore::TotalBytes() const
{
	int64 Total = 0;
	for (const TPair<FTerrainDigest, TArray<uint8>>& Pair : Objects)
	{
		Total += Pair.Value.Num();
	}
	return Total;
}

void FTerrainMemoryObjectStore::GetDigests(TArray<FTerrainDigest>& Out) const
{
	Out.Reset();
	Objects.GetKeys(Out);
}

// ---- apply / lookup / validate ------------------------------------------

ETerrainPersistError TerrainIndexApply(
	const FTerrainPersistIdentity& Identity,
	const ITerrainObjectStore& Source,
	ITerrainObjectStore& Sink,
	const FTerrainIndexRoot& OldRoot,
	const TArray<FTerrainIndexUpdate>& Updates,
	FTerrainIndexRoot& OutRoot,
	int32& OutPagesWritten)
{
	OutPagesWritten = 0;

	if (Updates.Num() == 0)
	{
		// Nothing changed: the previous root is still the complete, correct index.
		OutRoot = OldRoot;
		return ETerrainPersistError::None;
	}

	TArray<FSortedUpdate> Sorted;
	Sorted.Reserve(Updates.Num());
	for (const FTerrainIndexUpdate& Update : Updates)
	{
		if (!IsLeafValueConsistent(Update.Value))
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		if (static_cast<uint8>(Update.Value.Encoding) > static_cast<uint8>(ETerrainRegionEncoding::Empty))
		{
			return ETerrainPersistError::FieldOutOfRange;
		}

		FSortedUpdate Entry;
		Entry.Key   = TerrainIndexKeyFromChunk(Update.Key);
		Entry.Value = Update.Value;
		Sorted.Add(Entry);
	}

	Algo::Sort(Sorted, [](const FSortedUpdate& A, const FSortedUpdate& B) { return A.Key < B.Key; });

	for (int32 Index = 1; Index < Sorted.Num(); ++Index)
	{
		if (Sorted[Index].Key == Sorted[Index - 1].Key)
		{
			// Not last-write-wins. "Which of these two values did the caller mean" is not a
			// question a save format should answer by accident.
			return ETerrainPersistError::DuplicateKey;
		}
	}

	uint8 RootPrefix[TerrainPersistIndexKeyBytes] = {};
	FApplyContext Context{ Identity, Source, Sink };

	FTerrainIndexRoot NewRoot;
	const ETerrainPersistError Error = ApplySubtree(
		Context, 0, RootPrefix,
		OldRoot.bHasRootPage ? &OldRoot.RootPageDigest : nullptr,
		OldRoot.RootPageLength,
		Sorted, 0, Sorted.Num(),
		NewRoot.RootPageDigest, NewRoot.RootPageLength);
	if (Error != ETerrainPersistError::None)
	{
		return Error;
	}

	NewRoot.bHasRootPage = true;
	OutRoot = NewRoot;
	OutPagesWritten = Context.PagesWritten;
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainIndexLookup(
	const FTerrainPersistIdentity& Identity,
	const ITerrainObjectStore& Source,
	const FTerrainIndexRoot& Root,
	const FTerrainChunkKey& Key,
	FTerrainIndexLeafValue& OutValue,
	bool& bOutFound)
{
	bOutFound = false;

	if (!Root.bHasRootPage)
	{
		return ETerrainPersistError::None;
	}

	const FTerrainIndexKey IndexKey = TerrainIndexKeyFromChunk(Key);

	FTerrainDigest Digest = Root.RootPageDigest;
	uint32 Length = Root.RootPageLength;
	uint8 Prefix[TerrainPersistIndexKeyBytes] = {};

	for (int32 Depth = 0; Depth <= TerrainIndexLeafDepth; ++Depth)
	{
		FTerrainIndexPage Page;
		const ETerrainPersistError LoadError =
			LoadPage(Identity, Source, Digest, Length, Depth, Prefix, Page);
		if (LoadError != ETerrainPersistError::None)
		{
			return LoadError;
		}

		const uint8 Wanted = IndexKey.Bytes[Depth];

		if (Page.bLeaf)
		{
			for (const FTerrainIndexLeafEntry& Entry : Page.Leaves)
			{
				if (Entry.ByteValue == Wanted)
				{
					OutValue  = Entry.Value;
					bOutFound = true;
					return ETerrainPersistError::None;
				}
			}
			return ETerrainPersistError::None;   // never edited: regenerate from the base
		}

		const FTerrainIndexInternalEntry* Next = nullptr;
		for (const FTerrainIndexInternalEntry& Entry : Page.Internal)
		{
			if (Entry.ByteValue == Wanted)
			{
				Next = &Entry;
				break;
			}
		}
		if (Next == nullptr)
		{
			return ETerrainPersistError::None;
		}

		Digest = Next->ChildDigest;
		Length = Next->ChildLength;
		Prefix[Depth] = Wanted;
	}

	// Unreachable: the loop returns at the leaf depth.
	return ETerrainPersistError::FieldOutOfRange;
}

ETerrainPersistError TerrainIndexValidate(
	const FTerrainPersistIdentity& Identity,
	const ITerrainObjectStore& Source,
	const FTerrainIndexRoot& Root,
	FTerrainIndexValidation& OutStats,
	TArray<FTerrainChunkKey>* OutKeys)
{
	OutStats = FTerrainIndexValidation();
	if (OutKeys != nullptr)
	{
		OutKeys->Reset();
	}

	if (!Root.bHasRootPage)
	{
		// The legal empty G=0 checkpoint published at world creation, before any edit.
		return ETerrainPersistError::None;
	}

	uint8 RootPrefix[TerrainPersistIndexKeyBytes] = {};
	return ValidateSubtree(
		Identity, Source, 0, RootPrefix, Root.RootPageDigest, Root.RootPageLength, OutStats, OutKeys);
}
