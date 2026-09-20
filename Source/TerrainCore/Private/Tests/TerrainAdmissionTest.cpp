// Copyright VoxelWorld. Stateful adversarial tests of §4.11, no UWorld/plugin.
#if WITH_DEV_AUTOMATION_TESTS
#include "TerrainEditQueue.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainAdmissionTest,"TerrainCore.Admission.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FTerrainAdmissionTest::RunTest(const FString&)
{
	FTerrainOp Op; Op.RadiusVoxQ16=65536; Op.SourceId=999; Op.OpSeq=999; Op.TransactionId=999;
	TArray<FTerrainOp> Committed; TArray<FTerrainEditReceipt> Receipts;
	int32 Applied=0; bool Allowed=true, BackendSucceeds=true;
	FTerrainQueueCallbacks Cb;
	Cb.Validate=[&](const FTerrainOp&,const FTerrainSourceState&) { return Allowed ? ETerrainEditRejection::None : ETerrainEditRejection::PermissionDenied; };
	Cb.Apply=[&](const FTerrainOp&,FTerrainEditResult& R) { if (!BackendSucceeds) return false; ++Applied; R.VoxelsTouched=1; return true; };
	Cb.Commit=[&](const FTerrainOp& P,const FTerrainEditResult&) { Committed.Add(P); };
	Cb.Receipt=[&](uint32,const FTerrainEditReceipt& R) { Receipts.Add(R); };
	FTerrainSourceState State;
	FTerrainEditQueue Queue; Queue.Burst=1000; Queue.RatePerSecond=1000;
	Queue.RegisterSource(1,State); Queue.RegisterSource(2,State);
	FTerrainEditReceipt R;
	TestTrue(TEXT("First request reserves"),Queue.Submit(1,1,Op,1,Cb,R));
	TestTrue(TEXT("Queued duplicate is not a second job"),Queue.Submit(1,1,Op,1,Cb,R));
	TestEqual(TEXT("One pending op"),Queue.Depth(),1);
	Queue.Submit(1,2,Op,1,Cb,R); Queue.Submit(2,1,Op,1,Cb,R);
	Queue.Pump(1,Cb,32,1.);
	TestEqual(TEXT("Exactly three operations"),Applied,3);
	TestTrue(TEXT("Round robin prevents same-source starvation"),Committed.Num()==3 && Committed[0].SourceId==1 && Committed[1].SourceId==2 && Committed[2].SourceId==1);
	TestEqual(TEXT("Server overwrites supplied sequence"),Committed[0].OpSeq,FTerrainOpSeq(1));
	TestTrue(TEXT("Resolved retry returns original receipt"),Queue.Submit(1,1,Op,2,Cb,R));
	TestEqual(TEXT("Resolved retry uses original sequence"),R.OpSeq,int64(1));
	TestEqual(TEXT("Resolved retry does not execute"),Applied,3);
	Queue.Submit(1,3,Op,2,Cb,R); Allowed=false; Queue.Pump(2,Cb,32,1.);
	TestTrue(TEXT("Commit rechecks changed permission"),Receipts.Last().Rejection==ETerrainEditRejection::Revalidation);
	TestEqual(TEXT("Revalidation causes no mutation"),Applied,3); Allowed=true;
	BackendSucceeds=false; Queue.Submit(1,4,Op,2,Cb,R); Queue.Pump(2,Cb,32,1.); BackendSucceeds=true;
	TestTrue(TEXT("Backend refusal is explicit"),Receipts.Last().Rejection==ETerrainEditRejection::BackendFailed);
	TestEqual(TEXT("Rejected work consumes no sequence"),Queue.NextSequence(),FTerrainOpSeq(4));
	for (int64 Id=5;Id<=70;++Id) { Queue.Submit(1,Id,Op,3,Cb,R); Queue.Pump(3,Cb,32,1.); }
	TestFalse(TEXT("Evicted receipt cannot be executed again"),Queue.Submit(1,1,Op,4,Cb,R));
	TestTrue(TEXT("Evicted identity is stale"),R.Rejection==ETerrainEditRejection::StaleRequest);
	Queue.Submit(1,71,Op,4,Cb,R); Queue.Disconnect(1); const int32 Before=Applied; Queue.Pump(4,Cb,32,1.);
	TestEqual(TEXT("Disconnected queued source cannot mutate"),Applied,Before);

	FTerrainEditQueue Limited; Limited.GlobalLimit=2; Limited.SourceLimit=1;
	Limited.RegisterSource(1,State); Limited.RegisterSource(2,State); Limited.RegisterSource(3,State);
	Limited.Submit(1,1,Op,0,Cb,R);
	TestFalse(TEXT("Per-source queue cap"),Limited.Submit(1,2,Op,0,Cb,R));
	TestTrue(TEXT("Per-source cap reason"),R.Rejection==ETerrainEditRejection::QueueFull);
	Limited.Submit(2,1,Op,0,Cb,R);
	TestFalse(TEXT("Global queue cap"),Limited.Submit(3,1,Op,0,Cb,R));
	Limited.Cancel(Cb); TestEqual(TEXT("Draining discards pending work"),Limited.Depth(),0);
	TestTrue(TEXT("Draining receipts say ShuttingDown"),Receipts.Last().Rejection==ETerrainEditRejection::ShuttingDown);

	FTerrainEditQueue Charges; State.Charges=1; State.ChargePerOp=1; Charges.RegisterSource(1,State);
	TestTrue(TEXT("Reserve available charge"),Charges.Submit(1,1,Op,0,Cb,R));
	TestFalse(TEXT("Second request cannot spend reserved charge"),Charges.Submit(1,2,Op,0,Cb,R));
	Charges.Cancel(Cb);
	TestTrue(TEXT("Cancellation releases charge reservation"),Charges.Submit(1,3,Op,0,Cb,R));
	Charges.Pump(0,Cb,32,1.);
	TestFalse(TEXT("Committed charge cannot be reused"),Charges.Submit(1,4,Op,0,Cb,R));

	FTerrainEditQueue Rate; State={}; Rate.Burst=1; Rate.RatePerSecond=1; Rate.RegisterSource(1,State);
	Rate.Submit(1,1,Op,0,Cb,R); Rate.Pump(0,Cb,32,1.);
	TestFalse(TEXT("Rate tokens are consumed"),Rate.Submit(1,2,Op,.1,Cb,R));
	TestTrue(TEXT("Rate reason"),R.Rejection==ETerrainEditRejection::RateLimited);
	TestTrue(TEXT("Tokens refill with time"),Rate.Submit(1,3,Op,1.1,Cb,R)); Rate.Cancel(Cb);

	FTerrainEditQueue Split; Split.RegisterSource(1,State); Split.RegisterSource(2,State);
	FTerrainOp Box=Op; Box.Shape=ETerrainShape::Box; Box.ExtentVox=FIntVector(21);
	const int32 Start=Committed.Num();
	TestTrue(TEXT("Split admits all parts"),Split.Submit(1,1,Box,0,Cb,R));
	Split.Submit(2,1,Op,0,Cb,R); Split.Pump(0,Cb,1,1.); Split.Pump(0,Cb,32,1.);
	TestTrue(TEXT("Split sequence contiguous even across pump boundaries"),Committed.Num()==Start+3
		&& Committed[Start].TransactionId==Committed[Start+1].TransactionId
		&& Committed[Start].OpSeq+1==Committed[Start+1].OpSeq && Committed[Start+2].SourceId==2);
	return true;
}
#endif
