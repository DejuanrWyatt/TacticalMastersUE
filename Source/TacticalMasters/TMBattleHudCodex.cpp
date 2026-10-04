// The Codex: the Unit Guide's pages beyond the classes and the items. How to
// play, the words the game uses, every status, the hazard grounds and terrain
// (with the look each hazard has in each theme), the objectives, the camps and
// items, and the controls. Asked for by the human (2026-09-30: "make a sort of
// codex that contains all relevant information to the game"), in the game.
//
// Every number here is the rules' own default (SimTypes.h FTuning and the
// constants it names); the statuses come from the rules' list (AllStatuses),
// with the words the HUD already shows for them (TMBattleHudStyle.h StatusLook).
// When the rules change, change this too.

#include "TMBattleHud.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

#include "TMBattleDirector.h"
#include "TMBattleHudStyle.h"
#include "TMSettings.h"
#include "SimTypes.h"

using namespace TMHudStyle;

namespace TMCodex
{
	/** One row of a page: a word and what it means, or a heading over the rows after it. */
	struct FEntry
	{
		FString Term;
		FString Body;
		FLinearColor Colour = FLinearColor(0.9f, 0.92f, 0.96f);
		/** An icon under Content/Data/Icons, drawn before the word if there is one. */
		FString Picture;
		/** A short dim word after the term: "buff", "debuff". */
		FString Note;
		bool bHeading = false;
	};

	FEntry Heading(const TCHAR* Title)
	{
		FEntry Made;
		Made.Term = Title;
		Made.bHeading = true;
		return Made;
	}

	FEntry Row(const TCHAR* Term, const FString& Body, const FLinearColor& Colour = FLinearColor(0.9f, 0.92f, 0.96f))
	{
		FEntry Made;
		Made.Term = Term;
		Made.Body = Body;
		Made.Colour = Colour;
		return Made;
	}

	/** A paragraph across the whole width, no word before it. */
	FEntry Para(const FString& Body)
	{
		FEntry Made;
		Made.Body = Body;
		return Made;
	}

	const FLinearColor BurnCue(1.0f, 0.5f, 0.15f);
	const FLinearColor SpringCue(0.35f, 0.95f, 1.0f);

	void PageBasics(TArray<FEntry>& Out)
	{
		Out.Add(Heading(TEXT("Winning")));
		Out.Add(Row(TEXT("Knock them all out"), TEXT("Two sides, Blue and Red, field four units each. A side wins when the other has no unit standing. Both falling at once is a draw.")));
		Out.Add(Row(TEXT("Hold the middle"), TEXT("If the battle's setup turns it on: have a unit stand within 4 m of the map's centre, with no enemy there, for the set time in all. The time adds up over visits; with both sides inside, nobody gains.")));
		Out.Add(Row(TEXT("Time limit"), TEXT("If the setup sets one: when it runs out, the side with the larger share of its total health left wins (within 1% is a draw).")));
		Out.Add(Row(TEXT("Monsters"), TEXT("Neutral monsters fight both sides and never count towards a win.")));
		Out.Add(Heading(TEXT("Turns")));
		Out.Add(Row(TEXT("Turn Gauge"), TEXT("There are no rounds. Every unit's gauge fills as time passes, faster the more Speed it has. A full gauge makes the unit READY. The timeline at the top shows who comes next.")));
		Out.Add(Row(TEXT("Placing"), TEXT("If the setup gives a planning phase, place your units near your start before the clock runs. Press Ready to begin sooner.")));
		Out.Add(Row(TEXT("A unit's turn"), TEXT("One move and one action, in either order. The action is an ability or capturing a watchtower. Flipping a toggle, picking up or equipping items and ending the turn are free; taking an item off is the whole turn.")));
		Out.Add(Row(TEXT("Sprint"), TEXT("Walk a quarter further instead of using the action. Not after acting.")));
		Out.Add(Row(TEXT("Countdown"), TEXT("A READY unit has 8 seconds, plus 2 for each point of Patience, to take its turn. When the time runs out, the turn is lost.")));
		Out.Add(Row(TEXT("Doing less"), TEXT("A turn that did less brings the next one sooner. Moved and acted: the gauge starts empty. Only one of the two: 20% full. Neither: 40%. A turn with no ability also fills the gauge 25% faster until the next one.")));
		Out.Add(Row(TEXT("Ultimate"), TEXT("Ability 4 needs a full meter: +20 for each other ability used, +5 each turn, and more as the unit takes damage. Using it empties the meter.")));
		Out.Add(Heading(TEXT("Moving")));
		Out.Add(Row(TEXT("Ground"), TEXT("Tiles are 2 m across, and the ground stands at levels. A step may climb or drop 2 levels at most; more is a cliff. Water and rock can't be walked on.")));
		Out.Add(Row(TEXT("Other units"), TEXT("Walk through your allies but not your enemies, and don't stop on anyone.")));
		Out.Add(Row(TEXT("Engaged"), TEXT("Within 1.8 m of an enemy a unit is engaged. Stepping away from it costs an extra metre of the move.")));
		Out.Add(Row(TEXT("Facing"), TEXT("A unit faces the way it last walked or aimed. A hit from the side does 10% more, and from behind 25% more.")));
		Out.Add(Row(TEXT("Waypoints"), TEXT("Hold the waypoint key (Ctrl) and click to steer a walk round fire, out of a tower's sight, or over an item, which it picks up on the way. Up to 4. Still one walk: its whole length counts against the move.")));
		Out.Add(Row(TEXT("Tanks hold the line"), TEXT("A unit whose first role is tank holds the ground around it: an enemy that walks within 1.8 m of it has to stop there, so nobody walks past a tank to the back line. A unit that starts its walk beside one can still walk away, paying to break off as usual. The walk area shows where a walk would end.")));
		Out.Add(Row(TEXT("Go To"), TEXT("Click beyond the walk area to send a unit there over several turns. Each turn it walks as far as it can, then ends its turn (or waits for you, if you pick that on its strip); the turn it arrives on is yours. It stops and hands you the turn if an enemy comes into sight, it is hurt, or the way is blocked: Keep going (G) carries on, Backspace cancels, any other order replaces it. Numbers on the ground mark where each turn ends.")));
		Out.Add(Row(TEXT("Planning ahead"), TEXT("Click one of your units while it waits to plan its next turn: a walk, then an ability aimed from where the walk ends. It runs the moment the turn begins. If the spot is taken or the target gone by then, the plan is dropped and the turn is yours.")));
		Out.Add(Row(TEXT("Plan a turn"), TEXT("In a unit's turn, the plan key (G) sets its walk and ability without doing them, so you can see what the ability will reach from the new spot. G again does both.")));
		Out.Add(Heading(TEXT("Seeing")));
		Out.Add(Row(TEXT("Fog of war"), TEXT("You see what your units see: anything within their Sight that nothing hides. You also see everything near a watchtower you hold. The rest is in fog.")));
		Out.Add(Row(TEXT("Line of sight"), TEXT("Rock, ruins and rising ground block the view. Every ability must see the spot it aims at, and ranged ones need a clear line to it.")));
		Out.Add(Row(TEXT("High ground"), TEXT("Damage is 10% more for each level above the target, and 10% less for each level below it, up to 3 levels.")));
		Out.Add(Heading(TEXT("Health")));
		Out.Add(Row(TEXT("Damage"), TEXT("An ability's number is its power. Height, flanking and statuses such as Marked multiply it, then it is halved. The target's Armor (against physical) or Resist (against magic) takes a share off: 30 / (30 + it), so 10 takes a quarter and 30 takes half. A hit always does at least 1.")));
		Out.Add(Row(TEXT("Numbers over units"), TEXT("Red: damage. Big and bold with a \"!\": a critical strike. Dark red with an ember glow: burn. Crimson: bleed. Green: healing; pale green: regen. A status's name rises in its own colour. Your own units' health bars show for 3 seconds whenever they deal or take damage or are healed, the health just lost flashing as it drains away.")));
		Out.Add(Row(TEXT("Evasion and crit"), TEXT("A damaging hit can be evaded, by the target's Evasion % against any kind of hit. One evasion in ten dodges it outright; the other nine graze, taking half and never critical. A hit that lands can crit for 50% more. Heals and support never miss.")));
		Out.Add(Row(TEXT("Knocked out"), TEXT("At 0 HP a unit is knocked out: its statuses end and any cast it was making fizzles. An ally can revive it within 12 seconds. After that it is gone, and what it wore goes back to its team's stash.")));
		Out.Add(Row(TEXT("Resting"), TEXT("A unit that goes two of its turns without taking damage heals 5% of its max HP at the start of each turn after that.")));
	}

	void PageKeywords(TArray<FEntry>& Out)
	{
		Out.Add(Heading(TEXT("Stats")));
		Out.Add(Row(TEXT("HP"), TEXT("Health. At 0 the unit is knocked out.")));
		Out.Add(Row(TEXT("Armor"), TEXT("Takes a share off physical damage: 30 / (30 + Armor). The unit's card shows the share.")));
		Out.Add(Row(TEXT("Resist"), TEXT("The same against magic damage.")));
		Out.Add(Row(TEXT("Evasion"), TEXT("The % chance to evade any hit: 1 in 10 of those dodge it, the rest graze for half. A class's Evasion is the higher of its old physical and magic evasion; items and buffs to either add to it.")));
		Out.Add(Row(TEXT("Crit"), TEXT("The % chance for a damaging hit to do 50% more.")));
		Out.Add(Row(TEXT("Speed"), TEXT("How fast the Turn Gauge fills.")));
		Out.Add(Row(TEXT("Move"), TEXT("How many metres the unit walks in a turn.")));
		Out.Add(Row(TEXT("Patience"), TEXT("Each point adds 2 seconds to the turn countdown.")));
		Out.Add(Row(TEXT("Sight"), TEXT("How far the unit sees, in metres.")));
		Out.Add(Heading(TEXT("Ability kinds")));
		Out.Add(Row(TEXT("Active"), TEXT("Used as the turn's action.")));
		Out.Add(Row(TEXT("Ultimate"), TEXT("Ability 4: needs a full meter, and empties it.")));
		Out.Add(Row(TEXT("Passive"), TEXT("Always on. Can't be used.")));
		Out.Add(Row(TEXT("Toggle"), TEXT("Switched on or off for free, once a turn. While it is on, its effect applies. A burning trail round its button says it is on.")));
		Out.Add(Row(TEXT("Aura"), TEXT("Always on. At the start of each unit's turn, a unit in its area gets its effect for 2 turns. An ally aura reaches allies (and its owner); an enemy aura reaches enemies.")));
		Out.Add(Row(TEXT("Channeled"), TEXT("Goes off now, and again at the start of each of the next few turns, which are spent on it. Losing a turn to the countdown cancels it.")));
		Out.Add(Heading(TEXT("What abilities do")));
		Out.Add(Row(TEXT("Physical"), TEXT("Damage that Armor reduces and Protect softens.")));
		Out.Add(Row(TEXT("Magic"), TEXT("Damage that Resist reduces and Shell softens.")));
		Out.Add(Row(TEXT("Heal"), TEXT("Gives back health, up to the unit's max. Never misses.")));
		Out.Add(Row(TEXT("Revive"), TEXT("Brings back a knocked-out ally with a share of its health.")));
		Out.Add(Row(TEXT("Support"), TEXT("Does no damage or healing: only its status, buffs or gauge change.")));
		Out.Add(Row(TEXT("Buff"), TEXT("Raises or lowers a stat for some of the target's turns. Casting it again stacks.")));
		Out.Add(Row(TEXT("Gauge change"), TEXT("Pushes the target's Turn Gauge forward or back by a share of a full one. No effect on a READY unit.")));
		Out.Add(Row(TEXT("Element"), TEXT("Fire, ice, lightning or water. Elements react with Wet, Oiled and Chilled (see Statuses).")));
		Out.Add(Heading(TEXT("Aiming")));
		Out.Add(Row(TEXT("Range"), TEXT("How far from the user it can be aimed, in metres.")));
		Out.Add(Row(TEXT("Area"), TEXT("The radius of a circle, or the half-width of a line, in metres.")));
		Out.Add(Row(TEXT("Melee"), TEXT("Range 1.8 m or less. Can't reach a flying enemy.")));
		Out.Add(Row(TEXT("Single target"), TEXT("Hits the one unit at the aim point.")));
		Out.Add(Row(TEXT("Circle"), TEXT("Hits everyone of the target side in the area round the aim point.")));
		Out.Add(Row(TEXT("Self"), TEXT("Aimed at the user's own spot; with an area, a circle round the user.")));
		Out.Add(Row(TEXT("Line"), TEXT("From the user to the aim point.")));
		Out.Add(Row(TEXT("Cone"), TEXT("A wedge from the user towards the aim point.")));
		Out.Add(Row(TEXT("Global"), TEXT("Every unit of the target side, anywhere.")));
		Out.Add(Row(TEXT("Dash"), TEXT("A line that also carries the user to its end, if that spot is free.")));
		Out.Add(Row(TEXT("Cooldown"), TEXT("How many of the user's turns before it can be used again.")));
		Out.Add(Row(TEXT("Cast time"), TEXT("Seconds before it goes off. The user can't move meanwhile, and its gauge doesn't fill. Aimed at a unit, it follows that unit. Only a knock-out stops a cast.")));
		Out.Add(Heading(TEXT("Specials")));
		Out.Add(Row(TEXT("Blink"), TEXT("The user teleports to the aim point.")));
		Out.Add(Row(TEXT("Swap"), TEXT("The user trades places with the unit hit.")));
		Out.Add(Row(TEXT("Tame"), TEXT("An easy or medium monster below half health fights for the user's side for 3 of its turns.")));
		Out.Add(Row(TEXT("Rewind"), TEXT("Once a battle: heals back the last damage the user took.")));
	}

	void PageStatuses(TArray<FEntry>& Out)
	{
		Out.Add(Para(TEXT("A status lasts a number of its holder's own turns, acting and counting down as each begins. Applied again, it keeps the longer time. "
			"A hit that is evaded applies nothing. Bosses ignore Stun, Sleep, Freeze, Knockdown, Stop, Charmed and Terrified, and other ills last half as long on them.")));
		int Count = 0;
		const TMSim::FStatusDef* All = TMSim::AllStatuses(Count);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			Out.Add(Heading(Pass == 0 ? TEXT("Buffs") : TEXT("Debuffs")));
			for (int i = 0; i < Count; ++i)
			{
				const TMSim::FStatusDef& Def = All[i];
				if (Def.bHarmful != (Pass == 1))
				{
					continue;
				}
				const FStatusLook& Look = StatusLook(Def.Id);
				FEntry Made = Row(UTF8_TO_TCHAR(Def.Name), Look.Desc, Look.Colour);
				Made.Picture = TEXT("statuses/") + FString(UTF8_TO_TCHAR(Def.Id));
				Out.Add(Made);
			}
		}
		Out.Add(Heading(TEXT("Elements")));
		Out.Add(Row(TEXT("Lightning on Wet"), TEXT("Stuns the Wet unit and every Wet unit within 2 m of it, on either side.")));
		Out.Add(Row(TEXT("Ice on Wet"), TEXT("Washes off Wet and freezes the unit for a turn.")));
		Out.Add(Row(TEXT("Fire on Oiled"), TEXT("50% more damage, and the unit burns for twice as long.")));
		Out.Add(Row(TEXT("Fire on Wet, Chilled"), TEXT("Dries the unit or thaws it. So does burning ground.")));
	}

	void PageGround(TArray<FEntry>& Out)
	{
		Out.Add(Heading(TEXT("Hazard grounds")));
		Out.Add(Para(TEXT("A glowing ring round a tile's edge marks a hazard: orange burns, cyan mends. "
			"It acts at the start of the turn of a unit standing on it, not as units walk over it.")));
		Out.Add(Row(TEXT("Burning ground"), TEXT("Loses 8% of its max HP at the start of each of its turns there. Dries Wet and thaws Chilled.\n"
			"Summer Meadow: smouldering grass.  Autumn Ruins: burning rubble.  Ashen Caldera: a lava vent.  Winter Pass: a fire pit in the snow.  Moonlit Glade: a witchfire ring."), BurnCue));
		Out.Add(Row(TEXT("Healing spring"), TEXT("Heals 8% of its max HP at the start of its turn there, then runs dry for 3 of that unit's turns (it says so on the board). A unit with Decay is hurt instead. Both numbers are in Developer Tools.\n"
			"Summer Meadow: a spring pool.  Autumn Ruins: an old fountain.  Ashen Caldera: a hot spring.  Winter Pass: a steaming ice spring.  Moonlit Glade: a fairy pool."), SpringCue));
		Out.Add(Heading(TEXT("Terrain")));
		Out.Add(Row(TEXT("Height"), TEXT("Walkable ground stands at levels. A step may climb or drop 2 levels; more is a cliff (fliers cross any). Higher ground hits harder and sees over lower.")));
		Out.Add(Row(TEXT("Water"), TEXT("Can't be walked or waded. It is low, so units see and aim across it.")));
		Out.Add(Row(TEXT("Tall grass"), TEXT("Thick grass over knee height. A unit standing in it can't be seen by an enemy more than 3 m away, so it can't be targeted from further off. Striking out, or being struck, gives it away until its next turn. Yours says \"hidden in grass\" under its feet.")));
		Out.Add(Row(TEXT("Rock and ruins"), TEXT("Can't be walked. Block sight and every line aimed through them.")));
		Out.Add(Row(TEXT("Hills"), TEXT("Ground rising between two points hides one from the other.")));
		Out.Add(Row(TEXT("Trees and grass"), TEXT("Only scenery: they block nothing.")));
		Out.Add(Heading(TEXT("On the ground")));
		Out.Add(Row(TEXT("Watchtower ring"), TEXT("How near a unit must stand to capture the tower, in its holder's colour (grey for nobody's).")));
		Out.Add(Row(TEXT("Centre ring"), TEXT("Where to stand to hold the middle, in the colour of the side holding it.")));
		Out.Add(Row(TEXT("Chest"), TEXT("Items to take, in the colour of the best one: grey common, green uncommon, blue rare, gold epic.")));
	}

	void PageObjectives(TArray<FEntry>& Out, const FLinearColor* Tiers)
	{
		Out.Add(Heading(TEXT("Watchtowers")));
		Out.Add(Row(TEXT("Capturing"), TEXT("Stand within 2.5 m of a tower, on ground within 2 levels of its foot, and use Capture. It is the turn's action and ends the turn; walking there first is allowed. Two such turns take the tower.")));
		Out.Add(Row(TEXT("Not while"), TEXT("an enemy or a monster stands within 2.5 m of it, or your side already holds it. If the other side starts capturing, your progress is wiped.")));
		Out.Add(Row(TEXT("Holding"), TEXT("Its beacon burns in your colour, and your side sees everything within 14 m of it, over any wall or hill. The other side can take it from you.")));
		Out.Add(Heading(TEXT("The middle")));
		Out.Add(Row(TEXT("Hold the middle"), TEXT("If the setup turns it on: a ring marks 4 m round the map's centre. Stand in it with no enemy there for the set time in all to win.")));
		Out.Add(Heading(TEXT("Neutral camps")));
		Out.Add(Row(TEXT("Camps"), TEXT("If the setup turns them on, monsters wait in camps, in mirrored pairs. They fight both sides. They wake at set times, with a warning a few seconds before. With Camp respawns on (the setup screen), they come back a while after they are cleared; off, once cleared they are gone.")));
		Out.Add(Row(TEXT("Loot"), TEXT("Clearing a camp leaves a chest. Easy camps leave a common item, medium ones uncommon, hard ones rare. The boss in the middle leaves an epic, a rare and an epic only it has.")));
		Out.Add(Row(TEXT("Temperaments"), TEXT("Docile monsters never start a fight. Skittish ones flee, and may escape off the map with their loot. Territorial and aggressive ones attack whoever comes near or into view. Patrols walk a route.")));
		Out.Add(Row(TEXT("Ambush"), TEXT("Hidden until a unit comes within 3 m.")));
		Out.Add(Row(TEXT("Pack hunter"), TEXT("Picks off whoever stands furthest from help.")));
		Out.Add(Row(TEXT("Scavenger"), TEXT("Goes for item carriers and chests, and takes items.")));
		Out.Add(Row(TEXT("Lookout"), TEXT("Its alarm wakes the nearest sleeping camp early.")));
		Out.Add(Row(TEXT("Unstoppable"), TEXT("Bosses: ignore hard control, and other ills last half as long.")));
		Out.Add(Row(TEXT("Stagger"), TEXT("Three hits from behind stagger it for a turn.")));
		Out.Add(Row(TEXT("Enrage"), TEXT("25% more damage below half health.")));
		Out.Add(Row(TEXT("Shrine"), TEXT("A unit that starts its turn within 1.5 m of the Shrine of the Brute gets Surge for 2 turns. The shrine then rests for 45 seconds.")));
		Out.Add(Row(TEXT("Monsters"), TEXT("A monster that walks home heals fully. Monsters can't be revived.")));
		Out.Add(Heading(TEXT("Items")));
		Out.Add(Row(TEXT("Carrying"), TEXT("Each unit carries up to 3 items, never two the same. An item's ability becomes the unit's ability 5, 6 or 7.")));
		Out.Add(Row(TEXT("Buying"), TEXT("With an item budget set, each side buys items before the battle: common 1 point, uncommon 2, rare 3. Epic items can only be found.")));
		Out.Add(Row(TEXT("Picking up"), TEXT("Walk onto a chest or dropped items (end a move within 1.5 m): everything there goes into your team's stash. Free.")));
		Out.Add(Row(TEXT("Equipping"), TEXT("Open Items (the action bar or the top right): pick an item in the stash, then an open slot on any of your units. Free, any time.")));
		Out.Add(Row(TEXT("Taking off"), TEXT("A worn item stays on. It comes off only on its wearer's turn, before it moves or acts, and that is the whole turn; it goes back to the stash.")));
		Out.Add(Row(TEXT("Lost items"), TEXT("A unit gone for good sends what it wore back to its team's stash. A monster's items fall where it fell.")));
		Out.Add(Row(TEXT("Common"), TEXT("The cheapest; easy camps."), Tiers[0]));
		Out.Add(Row(TEXT("Uncommon"), TEXT("Medium camps."), Tiers[1]));
		Out.Add(Row(TEXT("Rare"), TEXT("Hard camps and bosses."), Tiers[2]));
		Out.Add(Row(TEXT("Epic"), TEXT("Only found: the boss and its camp."), Tiers[3]));
	}

	void PageControls(TArray<FEntry>& Out)
	{
		const FTMSettings& Keys = FTMSettings::Get();
		auto Key = [&Keys](ETMAction Action) { return Keys.KeyName(Action); };
		Out.Add(Para(TEXT("Every key can be changed in Options.")));
		Out.Add(Heading(TEXT("Orders")));
		Out.Add(Row(TEXT("Move"), Key(ETMAction::Move)));
		Out.Add(Row(TEXT("Sprint"), Key(ETMAction::Sprint)));
		Out.Add(Row(TEXT("Abilities 1-4"), FString::Printf(TEXT("%s  %s  %s  %s"), *Key(ETMAction::Ability1), *Key(ETMAction::Ability2), *Key(ETMAction::Ability3),
			*Key(ETMAction::Ability4))));
		Out.Add(Row(TEXT("End turn"), Key(ETMAction::EndTurn)));
		Out.Add(Row(TEXT("Cancel"), Key(ETMAction::Cancel) + TEXT(", or a right click")));
		Out.Add(Row(TEXT("Next ready unit"), Key(ETMAction::NextUnit)));
		Out.Add(Row(TEXT("Waypoint"), Key(ETMAction::Waypoint) + TEXT(" held while clicking a walk")));
		Out.Add(Row(TEXT("Plan a turn / go"), Key(ETMAction::PlanTurn)));
		Out.Add(Row(TEXT("Undo plan step"), Key(ETMAction::PlanUndo)));
		Out.Add(Row(TEXT("Mouse"), TEXT("Left click to select, aim and confirm; click one of your waiting units to plan its turn. Items, Take and Capture are buttons on the screen.")));
		Out.Add(Heading(TEXT("Camera")));
		Out.Add(Row(TEXT("Pan"), FString::Printf(TEXT("%s %s %s %s, or drag with the middle button"), *Key(ETMAction::CamForward), *Key(ETMAction::CamLeft),
			*Key(ETMAction::CamBack), *Key(ETMAction::CamRight))));
		Out.Add(Row(TEXT("Up / down"), FString::Printf(TEXT("%s / %s"), *Key(ETMAction::CamUp), *Key(ETMAction::CamDown))));
		Out.Add(Row(TEXT("Turn"), FString::Printf(TEXT("%s / %s, or drag with the right button"), *Key(ETMAction::CamRotateLeft), *Key(ETMAction::CamRotateRight))));
		Out.Add(Row(TEXT("Zoom"), TEXT("Mouse wheel")));
		Out.Add(Row(TEXT("Centre on unit"), Key(ETMAction::CenterCamera)));
		Out.Add(Heading(TEXT("Screens")));
		Out.Add(Row(TEXT("Codex"), Key(ETMAction::UnitGuide)));
		Out.Add(Row(TEXT("Pause"), Key(ETMAction::Pause)));
		Out.Add(Row(TEXT("Combat log"), Key(ETMAction::Log)));
		Out.Add(Row(TEXT("Edit layout"), Key(ETMAction::EditLayout)));
		Out.Add(Row(TEXT("Chat (online)"), Key(ETMAction::Chat)));
	}
}

void ATMBattleHud::DrawCodexPage(ATMBattleDirector& From, float PX, float PY, float PW, float PH, float Top)
{
	using TMCodex::FEntry;
	TArray<FEntry> Entries;
	const FLinearColor Tiers[4] = { TierColour(0), TierColour(1), TierColour(2), TierColour(3) };
	switch (From.GuideTab)
	{
	case 2: TMCodex::PageBasics(Entries); break;
	case 3: TMCodex::PageKeywords(Entries); break;
	case 4: TMCodex::PageStatuses(Entries); break;
	case 5: TMCodex::PageGround(Entries); break;
	case 6: TMCodex::PageObjectives(Entries, Tiers); break;
	default: TMCodex::PageControls(Entries); break;
	}

	UFont* Font = GEngine->GetMediumFont();
	const float X = PX + 24.0f * S;
	const float RowW = PW - 72.0f * S;
	const float TermW = 270.0f * S;
	const float BodyScale = 0.48f * S;
	const float LineH = TextSize(TEXT("Ag"), Font, BodyScale).Y + 2.0f * S;
	const float Bottom = PY + PH - 44.0f * S;

	// Scrolled a row at a time; as many rows as fit from there.
	const int32 Count = Entries.Num();
	From.GuideListScroll = FMath::Clamp(From.GuideListScroll, 0, FMath::Max(0, Count - 1));
	float Y = Top;
	int32 Drawn = 0;
	for (int32 r = From.GuideListScroll; r < Count; ++r)
	{
		const FEntry& Entry = Entries[r];
		if (Entry.bHeading)
		{
			const float H = 44.0f * S;
			if (Drawn > 0 && Y + H > Bottom)
			{
				break;
			}
			Text(Entry.Term, X, Y + 8.0f * S, Gold, Font, 0.66f * S);
			DrawRect(FLinearColor(Gold.R, Gold.G, Gold.B, 0.35f), X, Y + H - 6.0f * S, RowW, 1.0f);
			Y += H;
			++Drawn;
			continue;
		}
		const bool bWide = Entry.Term.IsEmpty();
		const float BodyX = bWide ? X : X + TermW;
		const TArray<FString> Lines = Wrap(Entry.Body, Font, BodyScale, RowW - (BodyX - X));
		const float H = FMath::Max(30.0f * S, Lines.Num() * LineH) + 10.0f * S;
		if (Drawn > 0 && Y + H > Bottom)
		{
			break;
		}
		if (!bWide)
		{
			float TermX = X;
			if (!Entry.Picture.IsEmpty())
			{
				if (UTexture2D* Picture2D = Icon(Entry.Picture, false))
				{
					Picture(Picture2D, X, Y - 2.0f * S, 28.0f * S, 28.0f * S);
				}
				else
				{
					DrawRect(Entry.Colour, X + 4.0f * S, Y + 2.0f * S, 20.0f * S, 20.0f * S);
				}
				TermX += 38.0f * S;
			}
			else if (Entry.Colour != FLinearColor(0.9f, 0.92f, 0.96f))
			{
				// A swatch for the coloured ones: the hazard rings, the tiers.
				DrawRect(Entry.Colour, X, Y + 4.0f * S, 6.0f * S, 18.0f * S);
				TermX += 16.0f * S;
			}
			Text(Entry.Term, TermX, Y, Entry.Colour, Font, 0.54f * S);
		}
		for (int32 l = 0; l < Lines.Num(); ++l)
		{
			Text(Lines[l], BodyX, Y + 2.0f * S + l * LineH, bWide ? Dim : TextColour, Font, BodyScale);
		}
		Y += H;
		++Drawn;
	}
	From.GuideListPage = FMath::Max(1, Drawn);
	ScrollBar(PX + PW - 22.0f * S, Top, Bottom - Top, static_cast<float>(Drawn), static_cast<float>(Count), static_cast<float>(From.GuideListScroll));
	Text(From.GuideListScroll + Drawn < Count ? FString(TEXT("More below: wheel, arrows or Page Up / Down to scroll."))
		: FString(TEXT("Wheel, arrows or Page Up / Down to scroll.")), X, PY + PH - 32.0f * S, Dim, Font, 0.48f * S);
}
