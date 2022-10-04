namespace VSharp.Interpreter.IL

open System
open System.IO
open System.Text
open System.Collections.Generic

open VSharp
open VSharp.Core
open VSharp.Utils

open CilStateOperations
open ipOperations

type pob = {loc : codeLocation; lvl : uint; pc : pathCondition}
    with
    override x.ToString() = $"loc = {x.loc}; lvl = %d{x.lvl}; pc = %s{Print.PrintPC x.pc}"

type pobStatus =
    | Unknown
    | Witnessed of cilState
    | Unreachable
    
type statisticsDump =
    {
        time : TimeSpan
        internalFails : Exception list
        iies : InsufficientInformationException list
        coveringStepsInsideZone : uint
        nonCoveringStepsInsideZone : uint
        coveringStepsOutsideZone : uint
        nonCoveringStepsOutsideZone : uint
        topVisitedLocationsInZone : (codeLocation * uint) list
        topVisitedLocationsOutOfZone : (codeLocation * uint) list
    }

// TODO: move statistics into (unique) instances of code location!
type public SILIStatistics() =
    let startIp2currentIp = Dictionary<codeLocation, Dictionary<codeLocation, uint>>()
    let totalVisited = Dictionary<codeLocation, uint>()
    let visitedWithHistory = Dictionary<codeLocation, HashSet<codeLocation>>()
    let unansweredPobs = List<pob>()
    let mutable startTime = DateTime.Now
    let internalFails = List<Exception>()
    let iies = List<cilState>()

    let mutable coveringStepsInsideZone = 0u
    let mutable nonCoveringStepsInsideZone = 0u
    let mutable coveringStepsOutsideZone = 0u
    let mutable nonCoveringStepsOutsideZone = 0u

    let isHeadOfBasicBlock (codeLocation : codeLocation) =
        let method = codeLocation.method
        if method.HasBody then
            method.CFG.IsBasicBlockStart codeLocation.offset
        else false

    let printDict' placeHolder (d : Dictionary<codeLocation, uint>) sb (m : Method, locs) =
        let sb = PrettyPrinting.appendLine sb $"%s{placeHolder}Method = %s{m.FullName}: ["
        let sb = Seq.fold (fun sb (loc : codeLocation) ->
            PrettyPrinting.appendLine sb (sprintf "%s\t\t%s <- %d" placeHolder ((int loc.offset).ToString("X")) d.[loc])) sb locs
        PrettyPrinting.appendLine sb $"%s{placeHolder}]"

    let printDict placeHolder sb (d : Dictionary<codeLocation, uint>) =
        let keys = d.Keys
        let sortedKeys = keys |> Seq.sort |> Seq.groupBy (fun location -> location.method)
        Seq.fold (printDict' placeHolder d) sb sortedKeys

    let printPart (sb : StringBuilder) i (k : KeyValuePair<codeLocation, Dictionary<codeLocation, uint>>) =
        let sb = PrettyPrinting.appendLine sb $"Part %d{i}; Start from {k.Key}"
//        let sb = PrettyPrinting.appendLine sb
        printDict "\t\t" sb k.Value

    let pickTotalUnvisitedInCFG (currentLoc : codeLocation) : codeLocation option =
        let infinity = UInt32.MaxValue
        let method = currentLoc.method
        let suitable offset distance =
            let loc = { offset = offset; method = method }
            let numberOfVisit = Dict.getValueOrUpdate totalVisited loc (fun () -> 0u)
            distance <> infinity && distance <> 0u && numberOfVisit = 0u

        if method.HasBody then
            method.CFG.DistancesFrom currentLoc.offset
            |> Seq.sortBy (fun offsetDistancePair -> offsetDistancePair.Value)
            |> Seq.filter (fun offsetDistancePair -> suitable offsetDistancePair.Key.Offset offsetDistancePair.Value)
            |> Seq.tryHead
            |> Option.map (fun offsetDistancePair -> { offset = offsetDistancePair.Key.Offset; method = method })
        else None

    let pickUnvisitedWithHistoryInCFG (currentLoc : codeLocation) (history : codeLocation seq) : codeLocation option =
        let infinity = UInt32.MaxValue
        let method = currentLoc.method
        let suitable offset distance =
            let loc = { offset = offset; method = method }
            let totalHistory = Dict.getValueOrUpdate visitedWithHistory loc (fun () -> HashSet<_>())
            let validDistance = distance <> infinity && (distance <> 0u || method.CFG.SortedBasicBlocks.Count = 1)
            let emptyHistory = totalHistory.Count = 0
            let nontrivialHistory = not <| totalHistory.IsSupersetOf(history)
            validDistance && (emptyHistory || nontrivialHistory)

        if method.HasBody then
            method.CFG.DistancesFrom currentLoc.offset
            |> Seq.sortBy (fun offsetDistancePair -> offsetDistancePair.Value)
            |> Seq.filter (fun offsetDistancePair -> suitable offsetDistancePair.Key.Offset offsetDistancePair.Value)
            |> Seq.tryHead
            |> Option.map (fun offsetDistancePair -> { offset = offsetDistancePair.Key.Offset; method = method })
        else None

    let rememberForward (start : codeLocation) (current : codeLocation) (visited : codeLocation seq) =
        if isHeadOfBasicBlock current then
            let mutable startRefDict = ref null
            if not <| startIp2currentIp.TryGetValue(start, startRefDict) then
                startRefDict <- ref (Dictionary<codeLocation, uint>())
                startIp2currentIp.Add(start, startRefDict.Value)
            let startDict = startRefDict.Value

            let mutable currentRef = ref 0u
            if not <| startDict.TryGetValue(current, currentRef) then
                currentRef <- ref 0u
                startDict.Add(current, 0u)
            startDict.[current] <- currentRef.Value + 1u

            let mutable totalRef = ref 0u
            if not <| totalVisited.TryGetValue(current, totalRef) then
                totalRef <- ref 0u
                totalVisited.Add(current, 0u)

            if totalRef.Value = 0u then
                if current.method.InCoverageZone then coveringStepsInsideZone <- coveringStepsInsideZone + 1u
                else coveringStepsOutsideZone <- coveringStepsOutsideZone + 1u
            elif current.method.InCoverageZone then nonCoveringStepsInsideZone <- nonCoveringStepsInsideZone + 1u
            else nonCoveringStepsOutsideZone <- nonCoveringStepsOutsideZone + 1u

            totalVisited.[current] <- totalRef.Value + 1u

            let mutable historyRef = ref null
            if not <| visitedWithHistory.TryGetValue(current, historyRef) then
                historyRef <- ref <| HashSet<_>()
                visitedWithHistory.Add(current, historyRef.Value)
            historyRef.Value.UnionWith visited
            
    let printStatistics (writer : TextWriter) (statisticsDump : statisticsDump) =
        writer.WriteLine(
            "Total time: {0:00}:{1:00}:{2:00}.{3}.",
            statisticsDump.time.Hours,
            statisticsDump.time.Minutes,
            statisticsDump.time.Seconds,
            statisticsDump.time.Milliseconds)
        if not <| List.isEmpty statisticsDump.internalFails then
            writer.WriteLine()
            writer.WriteLine()
            writer.WriteLine("{0} error(s) occured!")
            statisticsDump.internalFails |> List.iter writer.WriteLine
        if not <| List.isEmpty statisticsDump.iies then
            writer.WriteLine()
            writer.WriteLine()
            writer.WriteLine("{0} branch(es) with insufficient input information!", iies.Count)
            statisticsDump.iies |> List.iter (fun iie -> writer.WriteLine iie.Message)

    member x.IsCovered (loc : codeLocation) =
       Dict.getValueOrUpdate totalVisited loc (fun () -> 0u) > 0u

    member x.TrackStepForward (s : cilState) =
        let startLoc = ip2codeLocation s.startingIP
        let currentLoc = ip2codeLocation (currentIp s)
        let visited = history s
        match startLoc, currentLoc with
        | Some startLoc, Some currentLoc ->
            rememberForward startLoc currentLoc visited
        | _ -> ()

    member x.TrackStepBackward (pob : pob) (cilState : cilState) =
        // TODO
        ()

    member x.AddUnansweredPob (p : pob) = unansweredPobs.Add(p)
    member x.Clear() =
        startIp2currentIp.Clear()
        totalVisited.Clear()
        unansweredPobs.Clear()
        internalFails.Clear()
        iies.Clear()

    member x.ExplorationStarted() =
        x.Clear()
        startTime <- DateTime.Now


    member x.PickTotalUnvisitedInMethod loc = pickTotalUnvisitedInCFG loc

    member x.PickUnvisitedWithHistoryInCFG (loc, history) = pickUnvisitedWithHistoryInCFG loc history

    member x.CurrentExplorationTime with get() = DateTime.Now - startTime

    member x.IncompleteStates with get() = iies

    member x.InternalFails with get() = internalFails
    
    member x.DumpStatistics() =
        let topN = 5
        let topVisitedByMethods =
            totalVisited
            |> Seq.groupBy (fun kvp -> kvp.Key.method)
            |> Seq.map (snd >> Seq.maxBy (fun kvp -> kvp.Value))
            |> Seq.sortByDescending (fun kvp -> kvp.Value)
        let topVisitedByMethodsInZone = topVisitedByMethods |> Seq.filter (fun kvp -> kvp.Key.method.InCoverageZone) |> Seq.truncate topN
        let topVisitedByMethodsOutOfZone = topVisitedByMethods |> Seq.filter (fun kvp -> not kvp.Key.method.InCoverageZone) |> Seq.truncate topN
        {
            time = DateTime.Now - startTime
            internalFails = internalFails |> List.ofSeq
            iies = iies |> Seq.map (fun s -> s.iie.Value) |> List.ofSeq
            coveringStepsInsideZone = coveringStepsInsideZone
            nonCoveringStepsInsideZone = nonCoveringStepsInsideZone
            coveringStepsOutsideZone = coveringStepsOutsideZone
            nonCoveringStepsOutsideZone = nonCoveringStepsOutsideZone
            topVisitedLocationsInZone = topVisitedByMethodsInZone |> Seq.map (|KeyValue|) |> List.ofSeq
            topVisitedLocationsOutOfZone = topVisitedByMethodsOutOfZone |> Seq.map (|KeyValue|) |> List.ofSeq
        }

    member x.PrintStatistics (writer : TextWriter) =
        printStatistics writer <| x.DumpStatistics()

    member x.PrintDebugStatistics (writer : TextWriter) =
        let dump = x.DumpStatistics()
        printStatistics writer dump
        writer.WriteLine("Covering steps inside coverage zone: {0}", dump.coveringStepsInsideZone)
        writer.WriteLine("Revisiting steps inside coverage zone: {0}", dump.nonCoveringStepsInsideZone)
        writer.WriteLine("Covering steps outside coverage zone: {0}", dump.coveringStepsOutsideZone)
        writer.WriteLine("Revisiting steps outside coverage zone: {0}", dump.nonCoveringStepsOutsideZone)
        if not <| List.isEmpty dump.topVisitedLocationsInZone then
            writer.WriteLine("Top {0} visited locations (one per method) in zone:", Seq.length dump.topVisitedLocationsInZone)
        for loc, times in dump.topVisitedLocationsInZone do
            writer.WriteLine("  offset {0} of {1}: {2} time{3}",
                                (int loc.offset).ToString("X"), loc.method.FullName, times,
                                (if times = 1u then "" else "s"))
        if not <| List.isEmpty dump.topVisitedLocationsOutOfZone then
            writer.WriteLine("Top {0} visited locations (one per method) out of zone:", Seq.length dump.topVisitedLocationsOutOfZone)
        for loc, times in dump.topVisitedLocationsOutOfZone do
            writer.WriteLine("  offset {0} of {1}: {2} time{3}",
                                (int loc.offset).ToString("X"), loc.method.FullName, times,
                                (if times = 1u then "" else "s"))
