// Copyright VoxelWorld. See Docs/proposals/P-013-gate-observe.md.

#include "TerrainService.h"
#include "TerrainSettings.h"
#include "TerrainCore.h"

#if !UE_BUILD_SHIPPING

#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/NavigationDataInterface.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"

/**
 * Terrain.Observe -- the 1F Gate-Observe pass, measured rather than guessed (T-134, P-013).
 *
 * Scripted trials through the real edit path (`RequestEdit`, the queue, group commit), at sites
 * away from the player, each with its own collision interest. A line trace every frame watches
 * what the physics scene believes is there:
 *
 *   - **collision latency:** from the edit's commit to the first frame the trace sees the new
 *     shape, for digs, fills and a large dig across chunks;
 *   - **the gap:** any frame where the trace hits NOTHING. That is the fall-through window from
 *     T-101A §2f, if it exists. Stale frames (the old shape still there) are counted separately:
 *     they are not a fall, they are standing on air for a moment;
 *   - **undermining:** a cavity dug below the surface. Nothing is simulated structurally, so the
 *     surface above should stay exactly where it was, and a trace up from the cavity should hit
 *     its roof;
 *   - **streaming:** the interest released, then re-acquired. The data never unloads (the plugin
 *     keeps every edit in memory), but collision only exists near an interest, so this measures
 *     how long it is gone and whether it comes back with the dig in it;
 *   - **navigation:** if the world has a navigation system with data around the site, whether and
 *     how fast a dig reaches it. Run with `-TerrainNavmesh` and the ini in P-013 to switch one on.
 *
 * Runs in a standalone game, or on a dedicated server started with `-TerrainObserve`, which lets
 * the console edit path run there. The server is the case that matters: it has no camera, so
 * collision exists only where an interest asks for it.
 *
 * Development only; not in shipping builds. Prints `Observe:` lines and one `**** Terrain.Observe`
 * summary.
 */

namespace
{
	enum class EObserveKind : uint8 { Dig, Fill, BigDig, Undermine, Stream };

	struct FObserveTrial
	{
		EObserveKind Kind;
		FVector2D Offset;   // from the PlayerStart, cm
		double RadiusCm;
		const TCHAR* Name;
	};

	// Far enough from the PlayerStart that the player's own interest does not cover the site, and
	// far enough from each other that no two trials share a chunk. Chunks are 16 m.
	const FObserveTrial Trials[] = {
		{EObserveKind::Dig,       FVector2D( 4000,  4000), 150, TEXT("dig r=1.5 m")},
		{EObserveKind::Dig,       FVector2D( 4000, -4000), 150, TEXT("dig r=1.5 m")},
		{EObserveKind::Dig,       FVector2D(-4000,  4000), 150, TEXT("dig r=1.5 m")},
		{EObserveKind::Dig,       FVector2D(-4000, -4000), 300, TEXT("dig r=3 m")},
		{EObserveKind::Fill,      FVector2D( 6400,     0), 150, TEXT("fill r=1.5 m")},
		{EObserveKind::Fill,      FVector2D(-6400,     0), 150, TEXT("fill r=1.5 m")},
		{EObserveKind::BigDig,    FVector2D( 3200,  6400), 800, TEXT("dig r=8 m on a chunk corner")},
		{EObserveKind::Undermine, FVector2D(-3200,  6400), 250, TEXT("cavity r=2.5 m, 6 m down")},
		// The streaming pair: a dig at a site nothing else is near, then its interest released and
		// re-acquired. 80 m from every other site, so no other interest keeps its chunks alive.
		{EObserveKind::Dig,       FVector2D(    0, -9600), 150, TEXT("dig r=1.5 m, alone")},
		{EObserveKind::Stream,    FVector2D(    0, -9600),   0, TEXT("release and re-acquire that dig's interest")},
	};

	constexpr double TraceUpCm = 3000.0, TraceDownCm = 5000.0, PhaseTimeout = 8.0;

	// Smaller than the spacing between sites, so each trial's collision is its own interest's.
	constexpr double InterestRadiusCm = 1000.0;

	class FTerrainObserver : public TSharedFromThis<FTerrainObserver>
	{
	public:
		explicit FTerrainObserver(UWorld& InWorld) : World(&InWorld) {}

		void Start()
		{
			UTerrainService* Service = World->GetSubsystem<UTerrainService>();
			if (!Service || !Service->IsBackendReady())
			{
				UE_LOG(LogTerrainCore, Error, TEXT("**** Terrain.Observe: FAIL the terrain service is not ready ****"));
				return;
			}
			for (TActorIterator<APlayerStart> It(World.Get()); It; ++It) { Origin = It->GetActorLocation(); break; }

			UNavigationSystemBase* Nav = World->GetNavigationSystem();
			if (Nav)
			{
				FBox Bounds(ForceInit);
				for (const FObserveTrial& T : Trials)
				{
					Bounds += FVector(Origin.X + T.Offset.X, Origin.Y + T.Offset.Y, Origin.Z);
				}
				Nav->SetBuildBounds(Bounds.ExpandBy(FVector(2500, 2500, 6000)));
			}
			UE_LOG(LogTerrainCore, Display,
				TEXT("Observe: %d trials around the PlayerStart at %s; navigation system: %s"),
				int32(UE_ARRAY_COUNT(Trials)), *Origin.ToCompactString(), Nav ? *Nav->GetClass()->GetName() : TEXT("none"));

			TWeakPtr<FTerrainObserver> Weak = AsShared();
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
			{
				const TSharedPtr<FTerrainObserver> Self = Weak.Pin();
				return Self.IsValid() && Self->Tick();
			}));
			Keep = AsShared();   // alive until the ticker stops
		}

	private:
		enum class EPhase : uint8 { Begin, WaitCollision, Edit, Watch, NavWatch, StreamGone, StreamBack, Next };

		bool Tick()
		{
			if (!World.IsValid()) { Keep.Reset(); return false; }
			UTerrainService* Service = World->GetSubsystem<UTerrainService>();
			if (!Service || !Service->IsBackendReady()) { Finish(TEXT("FAIL the service went away")); return false; }
			if (Trial >= int32(UE_ARRAY_COUNT(Trials))) { Finish(nullptr); return false; }

			const FObserveTrial& T = Trials[Trial];
			const FVector Site(Origin.X + T.Offset.X, Origin.Y + T.Offset.Y, Origin.Z);
			const double Now = FPlatformTime::Seconds();
			++Frames;

			switch (Phase)
			{
			case EPhase::Begin:
				if (T.Kind == EObserveKind::Stream)
				{
					// Reuse the previous trial's site: it holds a dig, and its interest is still held.
					Service->ReleaseStreamingInterest(SiteInterests.FindRef(Trial - 1));
					SiteInterests.Remove(Trial - 1);
					Enter(EPhase::StreamGone, Now);
					break;
				}
				SiteInterests.Add(Trial, Service->AcquireStreamingInterest(Site, InterestRadiusCm, /*bCollision*/ true, /*bRender*/ false));
				FirstHitMs = -1.0;
				Enter(EPhase::WaitCollision, Now);
				break;

			case EPhase::WaitCollision:
			{
				double Z, ChunkM = 0;
				const bool bHit = TraceDown(Site, Z, &ChunkM);
				if (bHit && FirstHitMs < 0) FirstHitMs = (Now - PhaseStart) * 1000.0;
				if (bHit && Now - PhaseStart > 2.0)
				{
					// Two seconds, so the plugin's LOD update has run for the new interest: the first
					// trace can hit collision that existed before the interest asked for detail.
					SurfaceZ = Z;
					Results.FirstCollisionMs.Add(FirstHitMs);
					UE_LOG(LogTerrainCore, Display, TEXT("Observe: trial %d (%s): first collision %.0f ms after the interest; surface Z=%.0f; collision chunk about %.0f m across"),
						Trial, T.Name, FirstHitMs, SurfaceZ, ChunkM);
					SurfaceBefore = SurfaceZ;
					NavBefore = ProjectNav(FVector(Site.X, Site.Y, SurfaceZ));
					NavBeforeEver |= NavBefore;
					Enter(EPhase::Edit, Now);
				}
				else if (Now - PhaseStart > 15.0)
				{
					UE_LOG(LogTerrainCore, Warning, TEXT("Observe: trial %d (%s): no collision within 15 s; skipped"), Trial, T.Name);
					Enter(EPhase::Next, Now);
				}
				break;
			}

			case EPhase::Edit:
			{
				// The console source is rate-limited like any other (3 a second, burst 3): space the
				// trials so none is refused as RateLimited.
				if (Now - PhaseStart < 0.5) break;
				FTerrainEditRequest Request;
				Request.RadiusCm = T.RadiusCm;
				switch (T.Kind)
				{
				case EObserveKind::Fill:      Request.Kind = ETerrainEditKind::Add;    Request.WorldLocation = FVector(Site.X, Site.Y, SurfaceZ + 100.0); break;
				case EObserveKind::Undermine: Request.Kind = ETerrainEditKind::Remove; Request.WorldLocation = FVector(Site.X, Site.Y, SurfaceZ - 600.0); break;
				default:                      Request.Kind = ETerrainEditKind::Remove; Request.WorldLocation = FVector(Site.X, Site.Y, SurfaceZ); break;
				}
				FTerrainEditReceipt Receipt;
				if (!Service->RequestEdit(Request, Receipt))
				{
					UE_LOG(LogTerrainCore, Warning, TEXT("Observe: trial %d (%s): edit refused (%d); skipped"), Trial, T.Name, int32(Receipt.Rejection));
					Enter(EPhase::Next, Now);
					break;
				}
				// The edit is durable and published here: RequestEdit pumps it through group commit.
				Gap = Stale = 0;
				Enter(EPhase::Watch, Now);
				break;
			}

			case EPhase::Watch:
			{
				double Z;
				const bool bHit = TraceDown(Site, Z);
				bool bUpdated = false;
				if (!bHit) ++Gap;
				else
				{
					switch (T.Kind)
					{
					case EObserveKind::Fill:      bUpdated = Z > SurfaceBefore + T.RadiusCm * 0.5; break;
					case EObserveKind::Undermine: bUpdated = true; break;   // judged below, once the cook has had time
					default:                      bUpdated = Z < SurfaceBefore - T.RadiusCm * 0.5; break;
					}
					if (!bUpdated) ++Stale;
				}
				if (T.Kind == EObserveKind::Undermine)
				{
					if (Now - PhaseStart < 1.0) break;   // give the cook a full second, then look
					double Roof;
					const bool bRoof = TraceUp(FVector(Site.X, Site.Y, SurfaceBefore - 600.0), Roof);
					UE_LOG(LogTerrainCore, Display,
						TEXT("Observe: trial %d (%s): surface above %s (Z=%.0f, was %.0f); trace up from the cavity %s%s; gap frames %d"),
						Trial, T.Name, bHit ? TEXT("still solid") : TEXT("GONE"), bHit ? Z : 0.0, SurfaceBefore,
						bRoof ? TEXT("hits a roof at Z=") : TEXT("hits NOTHING"),
						bRoof ? *FString::Printf(TEXT("%.0f"), Roof) : TEXT(""), Gap);
					Results.Undermine = bHit && FMath::Abs(Z - SurfaceBefore) < 30.0 && bRoof;
					Enter(EPhase::Next, Now);
					break;
				}
				if (bUpdated)
				{
					const double Ms = (Now - PhaseStart) * 1000.0;
					UE_LOG(LogTerrainCore, Display,
						TEXT("Observe: trial %d (%s): collision shows the edit after %.0f ms (%d frames; stale %d, gap %d); new Z=%.0f"),
						Trial, T.Name, Ms, Frames, Stale, Gap, Z);
					Results.LatencyMs.Add(Ms);
					Results.GapFrames += Gap;
					Results.StaleFrames += Stale;
					if (T.Kind == EObserveKind::Dig && NavBefore)
					{
						NavTarget = FVector(Site.X, Site.Y, Z);
						Enter(EPhase::NavWatch, Now);
					}
					else Enter(EPhase::Next, Now);
				}
				else if (Now - PhaseStart > PhaseTimeout)
				{
					double StaleZ = 0, ChunkM = 0;
					TraceDown(Site, StaleZ, &ChunkM);
					UE_LOG(LogTerrainCore, Warning, TEXT("Observe: trial %d (%s): collision did NOT show the edit within %.0f s (stale %d, gap %d); still Z=%.0f on a chunk about %.0f m across"),
						Trial, T.Name, PhaseTimeout, Stale, Gap, StaleZ, ChunkM);
					Results.Timeouts++;
					Results.GapFrames += Gap;
					Enter(EPhase::Next, Now);
				}
				break;
			}

			case EPhase::NavWatch:
			{
				// The dig's floor, projected onto the navmesh: it lands at the old surface until the
				// tile is rebuilt, then drops into the pit.
				FVector Projected;
				if (ProjectNav(NavTarget, &Projected) && Projected.Z < SurfaceBefore - T.RadiusCm * 0.5)
				{
					const double Ms = (Now - PhaseStart) * 1000.0;
					UE_LOG(LogTerrainCore, Display, TEXT("Observe: trial %d (%s): navmesh shows the dig after %.0f ms more (floor Z=%.0f)"),
						Trial, T.Name, Ms, Projected.Z);
					Results.NavMs.Add(Ms);
					Enter(EPhase::Next, Now);
				}
				else if (Now - PhaseStart > PhaseTimeout)
				{
					UE_LOG(LogTerrainCore, Warning, TEXT("Observe: trial %d (%s): navmesh did NOT show the dig within %.0f s"), Trial, T.Name, PhaseTimeout);
					Results.NavTimeouts++;
					Enter(EPhase::Next, Now);
				}
				break;
			}

			case EPhase::StreamGone:
			{
				if (Now - PhaseStart < 3.0) break;
				double Z;
				const bool bHit = TraceDown(Site, Z);
				UE_LOG(LogTerrainCore, Display, TEXT("Observe: trial %d (%s): 3 s after release, collision %s"),
					Trial, T.Name, bHit ? *FString::Printf(TEXT("is STILL there (Z=%.0f)"), Z) : TEXT("is gone"));
				Results.StreamUnloaded = !bHit;
				SiteInterests.Add(Trial, Service->AcquireStreamingInterest(Site, InterestRadiusCm, true, false));
				Enter(EPhase::StreamBack, Now);
				break;
			}

			case EPhase::StreamBack:
			{
				double Z;
				if (TraceDown(Site, Z))
				{
					UE_LOG(LogTerrainCore, Display,
						TEXT("Observe: trial %d (%s): collision back %.0f ms after re-acquiring, Z=%.0f (the dig left the floor at %.0f)"),
						Trial, T.Name, (Now - PhaseStart) * 1000.0, Z, LastDigFloor);
					Results.StreamBackMs = (Now - PhaseStart) * 1000.0;
					Results.StreamKeptEdit = FMath::Abs(Z - LastDigFloor) < 30.0;
					Enter(EPhase::Next, Now);
				}
				else if (Now - PhaseStart > 15.0)
				{
					UE_LOG(LogTerrainCore, Warning, TEXT("Observe: trial %d (%s): collision did not come back within 15 s"), Trial, T.Name);
					Enter(EPhase::Next, Now);
				}
				break;
			}

			case EPhase::Next:
				if (T.Kind == EObserveKind::Dig) { double Z; if (TraceDown(Site, Z)) LastDigFloor = Z; }
				++Trial;
				Enter(EPhase::Begin, Now);
				break;
			}
			return true;
		}

		void Enter(EPhase Next, double Now) { Phase = Next; PhaseStart = Now; Frames = 0; }

		bool TraceDown(const FVector& Site, double& OutZ, double* OutChunkM = nullptr) const
		{
			FHitResult Hit;
			const FVector From(Site.X, Site.Y, Origin.Z + TraceUpCm), To(Site.X, Site.Y, Origin.Z - TraceDownCm);
			if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility)) return false;
			OutZ = Hit.ImpactPoint.Z;
			// The size of the collision chunk hit says which level of detail the physics scene holds:
			// full detail is a 16 m chunk, and each coarser level doubles it.
			if (OutChunkM && Hit.GetComponent())
			{
				*OutChunkM = Hit.GetComponent()->Bounds.BoxExtent.GetMax() * 2.0 / 100.0;
			}
			return true;
		}

		bool TraceUp(const FVector& From, double& OutZ) const
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, From, From + FVector(0, 0, 2000), ECC_Visibility)) return false;
			OutZ = Hit.ImpactPoint.Z;
			return true;
		}

		bool ProjectNav(const FVector& Point, FVector* OutPoint = nullptr) const
		{
			const UNavigationSystemBase* Nav = World->GetNavigationSystem();
			const INavigationDataInterface* Data = Nav ? Nav->GetMainNavData() : nullptr;
			FNavLocation Location;
			if (!Data || !Data->ProjectPoint(Point, Location, FVector(100, 100, 600))) return false;
			if (OutPoint) *OutPoint = Location.Location;
			return true;
		}

		void Finish(const TCHAR* Failure)
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Handle);
			if (UTerrainService* Service = World.IsValid() ? World->GetSubsystem<UTerrainService>() : nullptr)
			{
				for (const TPair<int32, uint32>& Held : SiteInterests) Service->ReleaseStreamingInterest(Held.Value);
			}
			SiteInterests.Reset();
			if (Failure)
			{
				UE_LOG(LogTerrainCore, Error, TEXT("**** Terrain.Observe: %s ****"), Failure);
			}
			else
			{
				Results.LatencyMs.Sort();
				Results.NavMs.Sort();
				Results.FirstCollisionMs.Sort();
				const auto Span = [](const TArray<double>& V)
				{ return V.IsEmpty() ? FString(TEXT("none")) : FString::Printf(TEXT("%.0f-%.0f ms (median %.0f)"), V[0], V.Last(), V[V.Num() / 2]); };
				UE_LOG(LogTerrainCore, Display,
					TEXT("**** Terrain.Observe: first collision after a new interest %s; collision shows an edit %s over %d edits, %d timeouts; gap frames (no collision at all) %d; ")
					TEXT("stale frames %d; undermined surface %s; streaming: collision %s after release, back in %.0f ms %s; ")
					TEXT("navmesh: %s, %d timeouts%s ****"),
					*Span(Results.FirstCollisionMs), *Span(Results.LatencyMs), Results.LatencyMs.Num(), Results.Timeouts, Results.GapFrames, Results.StaleFrames,
					Results.Undermine ? TEXT("held (roof present)") : TEXT("did NOT hold as expected"),
					Results.StreamUnloaded ? TEXT("unloaded") : TEXT("stayed"), Results.StreamBackMs,
					Results.StreamKeptEdit ? TEXT("with the dig intact") : TEXT("WITHOUT the dig"),
					*Span(Results.NavMs), Results.NavTimeouts, NavBeforeEver ? TEXT("") : TEXT(" (no navigation data at the sites)"));
			}
			Keep.Reset();
		}

		TWeakObjectPtr<UWorld> World;
		TSharedPtr<FTerrainObserver> Keep;
		FTSTicker::FDelegateHandle Handle;
		FVector Origin = FVector::ZeroVector;
		int32 Trial = 0, Frames = 0, Gap = 0, Stale = 0;
		EPhase Phase = EPhase::Begin;
		double PhaseStart = 0, SurfaceZ = 0, SurfaceBefore = 0, LastDigFloor = 0, FirstHitMs = -1.0;
		bool NavBefore = false, NavBeforeEver = false;
		FVector NavTarget = FVector::ZeroVector;
		TMap<int32, uint32> SiteInterests;

		struct
		{
			TArray<double> LatencyMs, NavMs, FirstCollisionMs;
			int32 GapFrames = 0, StaleFrames = 0, Timeouts = 0, NavTimeouts = 0;
			bool Undermine = false, StreamUnloaded = false, StreamKeptEdit = false;
			double StreamBackMs = 0;
		} Results;

	};
}

static FAutoConsoleCommandWithWorld GTerrainObserveCommand(
	TEXT("Terrain.Observe"),
	TEXT("Development: the 1F Gate-Observe trials -- collision latency and gaps, undermining, ")
	TEXT("streaming and navigation after edits (T-134, P-013)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		// Three seconds' grace, so a command-line -ExecCmds does not run before the world is ready.
		TWeakObjectPtr<UWorld> Weak(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
		{
			if (UWorld* Live = Weak.Get()) MakeShared<FTerrainObserver>(*Live)->Start();
			return false;
		}), 3.0f);
	}));

#endif // !UE_BUILD_SHIPPING
