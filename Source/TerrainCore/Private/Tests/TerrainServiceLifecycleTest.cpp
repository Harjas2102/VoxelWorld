// Copyright VoxelWorld. See Docs/ARCHITECTURE.md §4.5.1.

#if WITH_DEV_AUTOMATION_TESTS

#include "TerrainService.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FLifetimeEvidence
	{
		TArray<FString> Events;
		bool bFieldAlive = true;
		bool bFieldAliveAtShutdown = false;
		bool bFieldAliveAtBackendDestruction = false;
		int32 UnexpectedCalls = 0;
		TFunction<void()> DuringShutdown;
	};

	class FObservedField final : public ITerrainDensityField
	{
	public:
		explicit FObservedField(FLifetimeEvidence& In) : Evidence(In) {}
		virtual ~FObservedField() override
		{
			Evidence.Events.Add(TEXT("field destroyed"));
			Evidence.bFieldAlive = false;
		}
		virtual FTerrainDensitySample Sample(FIntVector Position) const override { return {}; }
	private:
		FLifetimeEvidence& Evidence;
	};

	class FObservedBackend final : public ITerrainBackend
	{
	public:
		explicit FObservedBackend(FLifetimeEvidence& In) : Evidence(In) {}
		virtual ~FObservedBackend() override
		{
			Evidence.bFieldAliveAtBackendDestruction = Evidence.bFieldAlive;
			Evidence.Events.Add(TEXT("backend destroyed"));
		}
		virtual bool Initialize(const FTerrainBackendInit&) override { return true; }
		virtual void Shutdown() override
		{
			Evidence.bFieldAliveAtShutdown = Evidence.bFieldAlive;
			Evidence.Events.Add(TEXT("shutdown"));
			if (Evidence.DuringShutdown) Evidence.DuringShutdown();
		}
		virtual void ClearStreamingInterest(uint32 Id) override
		{
			Evidence.Events.Add(FString::Printf(TEXT("clear %u"), Id));
		}
		virtual bool ApplyOp(const FTerrainOp&, FTerrainEditResult&) override { ++Evidence.UnexpectedCalls; return false; }
		virtual bool ReadRegion(const FTerrainChunkKey&, FTerrainRegionData&) override { ++Evidence.UnexpectedCalls; return false; }
		virtual bool WriteRegion(const FTerrainRegionData&) override { ++Evidence.UnexpectedCalls; return false; }
		virtual uint64 HashRegion(const FTerrainChunkKey&) const override { ++Evidence.UnexpectedCalls; return 0; }
		virtual bool IsRegionResident(const FTerrainChunkKey&) const override { ++Evidence.UnexpectedCalls; return false; }
		virtual void FlushPendingWork() override { ++Evidence.UnexpectedCalls; }
		virtual bool QueryPoint(const FIntVector&, FTerrainPointSample&) const override { ++Evidence.UnexpectedCalls; return false; }
		virtual void SetStreamingInterest(const FTerrainStreamingInterest&) override { ++Evidence.UnexpectedCalls; }
	private:
		FLifetimeEvidence& Evidence;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainServiceLifecycleTest, "TerrainCore.Service.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FTerrainServiceLifecycleTest::RunTest(const FString& Parameters)
{
	// Headless §6.1: development-only friend installs an observed backend, no UWorld/plugin.
	FLifetimeEvidence Evidence;
	TStrongObjectPtr<UTerrainService> Service(NewObject<UTerrainService>());
	FTerrainEditRequest Request;
	FTerrainEditReceipt Receipt;
	TestFalse(TEXT("Constructed service refuses edits"), Service->RequestEdit(Request, Receipt));
	TestTrue(TEXT("Uninitialised reason is NotReady"), Receipt.Rejection == ETerrainEditRejection::NotReady);
	FSubsystemCollection<UWorldSubsystem> Collection;
	Service->Initialize(Collection);
	Service->RequestEdit(Request, Receipt);
	TestTrue(TEXT("Before world begin-play is NotReady"), Receipt.Rejection == ETerrainEditRejection::NotReady);
	Service->DensityField = MakeShared<FObservedField, ESPMode::ThreadSafe>(Evidence);
	Service->Backend = MakeUnique<FObservedBackend>(Evidence);
	Service->ActiveInit.DensityField = Service->DensityField.Get();
	Service->State = ETerrainServiceState::Ready;
	TestTrue(TEXT("Ready backend is visible"), Service->IsBackendReady());
	Service->RequestEdit(Request, Receipt);
	TestTrue(TEXT("Ready still enforces authority"), Receipt.Rejection == ETerrainEditRejection::NoAuthority);
	FTerrainStreamingInterest Interest;
	Interest.InterestId = 9;
	Service->Interests.Add(9, Interest);
	Evidence.DuringShutdown = [&]()
	{
		TestTrue(TEXT("State is Draining inside backend shutdown"), Service->State == ETerrainServiceState::Draining);
		TestFalse(TEXT("Draining backend is unavailable"), Service->IsBackendReady());
		Service->RequestEdit(Request, Receipt);
		TestTrue(TEXT("Draining rejects as ShuttingDown"), Receipt.Rejection == ETerrainEditRejection::ShuttingDown);
		TestFalse(TEXT("Shutdown rejection is not an applied edit"), Receipt.bApplied);
		TestEqual(TEXT("Shutdown rejection consumes no OpSeq"), Service->NextOpSeq, FTerrainOpSeq(1));
		FTerrainPointSample Sample;
		Sample.bResident = true;
		TestFalse(TEXT("Draining refuses queries"), Service->QueryPoint(FIntVector::ZeroValue, Sample));
		TestFalse(TEXT("Refused query clears output"), Sample.bResident);
		TestEqual(TEXT("Draining cannot acquire interest"),
			Service->AcquireStreamingInterest(FVector::ZeroVector, 100, true, false), uint32(0));
		Service->UpdateStreamingInterest(9, FVector(100));
		Service->ReleaseStreamingInterest(9);
		Service->DestroyBackend(); // Re-entry must not run teardown twice.
	};
	// An unrelated/null world signal must not shut down this fixture's live backend.
	FWorldDelegates::OnWorldBeginTearDown.Broadcast(nullptr);
	TestTrue(TEXT("Null world signal ignored"), Service->IsBackendReady());
	// Model the early teardown signal, which closes admission while actors still exist.
	Service->State = ETerrainServiceState::Draining;
	Service->RequestEdit(Request, Receipt);
	TestTrue(TEXT("Early draining rejects new work"), Receipt.Rejection == ETerrainEditRejection::ShuttingDown);
	TestTrue(TEXT("Early draining retains the field until backend release"), Evidence.bFieldAlive);
	Service->DestroyBackend();
	const TArray<FString> Expected = { TEXT("clear 9"), TEXT("shutdown"), TEXT("backend destroyed"), TEXT("field destroyed") };
	TestTrue(TEXT("Interests, backend, field released in specified order"), Evidence.Events == Expected);
	TestTrue(TEXT("Field outlives Shutdown"), Evidence.bFieldAliveAtShutdown);
	TestTrue(TEXT("Field also outlives backend destruction"), Evidence.bFieldAliveAtBackendDestruction);
	TestEqual(TEXT("No normal backend calls during teardown"), Evidence.UnexpectedCalls, 0);
	TestTrue(TEXT("Teardown reaches TornDown"), Service->State == ETerrainServiceState::TornDown);
	TestFalse(TEXT("Backend released"), Service->Backend.IsValid());
	TestFalse(TEXT("Field released"), Service->DensityField.IsValid());
	TestTrue(TEXT("Borrowed initialization pointer cleared"), Service->ActiveInit.DensityField == nullptr);
	TestEqual(TEXT("Interests released"), Service->GetStreamingInterestCount(), 0);
	Service->RequestEdit(Request, Receipt);
	TestTrue(TEXT("TornDown reason is NotReady"), Receipt.Rejection == ETerrainEditRejection::NotReady);
	Service->Deinitialize();
	Service->Deinitialize();
	TestTrue(TEXT("Repeated teardown does not touch released resources"), Evidence.Events == Expected);
	TestFalse(TEXT("World teardown hook released"), Service->WorldTearDownHandle.IsValid());

	// Startup never reached Ready: cancellation still finishes in TornDown safely.
	TStrongObjectPtr<UTerrainService> NeverReady(NewObject<UTerrainService>());
	NeverReady->Initialize(Collection);
	NeverReady->Deinitialize();
	TestTrue(TEXT("Teardown before startup is safe"), NeverReady->State == ETerrainServiceState::TornDown);
	NeverReady->RequestEdit(Request, Receipt);
	TestTrue(TEXT("Failed/not-started backend remains unavailable"), Receipt.Rejection == ETerrainEditRejection::NotReady);
    // Model a plugin generator instance retained by an in-flight mesher. Service
    // teardown cannot free the immutable field until that last consumer releases it.
    FLifetimeEvidence AsyncEvidence;
    TStrongObjectPtr<UTerrainService> AsyncService(NewObject<UTerrainService>());
    AsyncService->Initialize(Collection);
    AsyncService->DensityField=MakeShared<FObservedField,ESPMode::ThreadSafe>(AsyncEvidence);
    AsyncService->Backend=MakeUnique<FObservedBackend>(AsyncEvidence);
    AsyncService->ActiveInit.DensityFieldOwner=AsyncService->DensityField;
    auto PendingGenerator=AsyncService->DensityField;
    AsyncService->State=ETerrainServiceState::Ready;
    AsyncService->Deinitialize();
    TestTrue(TEXT("Outstanding immutable consumer retains field after teardown"),AsyncEvidence.bFieldAlive);
    PendingGenerator->Sample(FIntVector::ZeroValue);
    PendingGenerator.Reset();
    TestFalse(TEXT("Last consumer releases field"),AsyncEvidence.bFieldAlive);
	AddInfo(TEXT("Worldless lifecycle: state-specific rejection, re-entry, and ordered ownership release. Outstanding generator lifetime is retained; queue tests cover cancellation, MP harness covers repeated live travel."));
	return true;
}

#endif
