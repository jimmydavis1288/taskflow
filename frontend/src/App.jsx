import { useEffect, useRef, useState } from "react";
import {
  Activity,
  AlertCircle,
  ArrowDown,
  ArrowUp,
  ArrowUpRight,
  Check,
  CheckCircle2,
  ChevronRight,
  CircleDashed,
  Clock3,
  Code2,
  Command,
  Database,
  Filter,
  Layers3,
  LoaderCircle,
  Plus,
  RefreshCw,
  Search,
  Server,
  X,
  Zap,
} from "lucide-react";
import { createJob, getApiBaseUrl, getJob, getJobs, getStats } from "./api.js";

const STATUSES = ["ALL", "PENDING", "PROCESSING", "COMPLETED", "FAILED"];
const INITIAL_STATS = {
  total: null,
  completed: null,
  processing: null,
  pending: null,
  failed: null,
};

function validateStats(stats) {
  const keys = ["total", "completed", "processing", "pending", "failed"];
  if (
    !stats ||
    keys.some((key) => !Number.isFinite(stats[key]) || stats[key] < 0)
  ) {
    throw new Error("The TaskFlow API returned invalid job statistics.");
  }
  return stats;
}

function validateJobs(jobs) {
  if (!Array.isArray(jobs)) {
    throw new Error("The TaskFlow API returned an invalid jobs list.");
  }
  return jobs;
}

function formatDate(value) {
  if (!value) return "Not available";
  const date = new Date(value);
  return Number.isNaN(date.getTime())
    ? "Not available"
    : new Intl.DateTimeFormat(undefined, {
        dateStyle: "medium",
        timeStyle: "short",
      }).format(date);
}

function StatusBadge({ status }) {
  const style = {
    PENDING: "bg-amber-50 text-amber-700 ring-amber-200",
    PROCESSING: "bg-blue-50 text-blue-700 ring-blue-200",
    COMPLETED: "bg-emerald-50 text-emerald-700 ring-emerald-200",
    FAILED: "bg-rose-50 text-rose-700 ring-rose-200",
  }[status] || "bg-slate-100 text-slate-700 ring-slate-200";
  const Icon = {
    PENDING: Clock3,
    PROCESSING: LoaderCircle,
    COMPLETED: CheckCircle2,
    FAILED: AlertCircle,
  }[status] || CircleDashed;

  return (
    <span
      className={`inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-[11px] font-bold tracking-wide ring-1 ring-inset ${style}`}
    >
      <Icon
        aria-hidden="true"
        className={`h-3.5 w-3.5 ${status === "PROCESSING" ? "animate-spin" : ""}`}
      />
      {status || "UNKNOWN"}
    </span>
  );
}

function PriorityLabel({ priority }) {
  const detail = {
    HIGH: { className: "text-rose-600", icon: ArrowUp },
    MEDIUM: { className: "text-amber-600", icon: ArrowUpRight },
    LOW: { className: "text-slate-500", icon: ArrowDown },
  }[priority] || { className: "text-slate-500", icon: ArrowUpRight };
  const Icon = detail.icon;

  return (
    <span className={`inline-flex items-center gap-1.5 text-xs font-semibold ${detail.className}`}>
      <Icon aria-hidden="true" className="h-3.5 w-3.5" />
      {priority || "UNKNOWN"}
    </span>
  );
}

function StatCard({ label, value, icon: Icon, accent, footnote }) {
  return (
    <article className="rounded-2xl border border-slate-200/80 bg-white p-5 shadow-panel transition duration-200 hover:-translate-y-0.5 hover:shadow-lg">
      <div className="flex items-start justify-between">
        <p className="text-sm font-medium text-slate-500">{label}</p>
        <span className={`grid h-10 w-10 place-items-center rounded-xl ${accent}`}>
          <Icon aria-hidden="true" className="h-5 w-5" />
        </span>
      </div>
      <p className="font-display mt-5 text-3xl font-bold tracking-tight text-slate-900">
        {value === null ? (
          <span className="inline-block h-9 w-16 animate-pulse rounded-lg bg-slate-100" />
        ) : (
          value.toLocaleString()
        )}
      </p>
      <p className="mt-1.5 text-xs text-slate-400">{footnote}</p>
    </article>
  );
}

function SectionHeading({ eyebrow, title, description, action }) {
  return (
    <div className="flex flex-wrap items-end justify-between gap-4">
      <div>
        <p className="text-[11px] font-bold uppercase tracking-[0.18em] text-indigo-600">
          {eyebrow}
        </p>
        <h2 className="font-display mt-1 text-xl font-bold tracking-tight text-slate-900">
          {title}
        </h2>
        {description && <p className="mt-1 text-sm text-slate-500">{description}</p>}
      </div>
      {action}
    </div>
  );
}

function ApiError({ message, onRetry }) {
  if (!message) return null;
  return (
    <div
      className="flex flex-wrap items-center justify-between gap-3 rounded-xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-800"
      role="alert"
    >
      <div className="flex items-start gap-2.5">
        <AlertCircle aria-hidden="true" className="mt-0.5 h-4 w-4 shrink-0" />
        <span>{message}</span>
      </div>
      {onRetry && (
        <button
          className="inline-flex items-center gap-1 font-semibold underline decoration-rose-300 underline-offset-2 hover:text-rose-950"
          onClick={onRetry}
          type="button"
        >
          Retry <ArrowUpRight aria-hidden="true" className="h-3.5 w-3.5" />
        </button>
      )}
    </div>
  );
}

function App() {
  const [stats, setStats] = useState(INITIAL_STATS);
  const [jobs, setJobs] = useState([]);
  const [jobsLoaded, setJobsLoaded] = useState(false);
  const [jobsError, setJobsError] = useState("");
  const [statsError, setStatsError] = useState("");
  const [selectedJobId, setSelectedJobIdState] = useState(null);
  const [selectedJob, setSelectedJob] = useState(null);
  const [detailLoading, setDetailLoading] = useState(false);
  const [detailError, setDetailError] = useState("");
  const [filterStatus, setFilterStatus] = useState("ALL");
  const [searchQuery, setSearchQuery] = useState("");
  const [payload, setPayload] = useState("");
  const [priority, setPriority] = useState("MEDIUM");
  const [submitting, setSubmitting] = useState(false);
  const [submissionMessage, setSubmissionMessage] = useState(null);
  const [lastUpdated, setLastUpdated] = useState(null);
  const selectedJobIdRef = useRef(null);
  const refreshRef = useRef(null);

  function selectJobId(id) {
    selectedJobIdRef.current = id;
    setSelectedJobIdState(id);
    setSelectedJob(null);
    setDetailError("");
    setDetailLoading(Boolean(id));
  }

  useEffect(() => {
    let active = true;
    let inFlight = false;
    let timer;
    let controller;

    async function refresh() {
      if (!active || inFlight) return;
      inFlight = true;
      controller = new AbortController();
      const { signal } = controller;
      const selectedId = selectedJobIdRef.current;

      try {
        const results = await Promise.allSettled([
          getJobs(signal).then(validateJobs),
          getStats(signal).then(validateStats),
          selectedId ? getJob(selectedId, signal) : Promise.resolve(null),
        ]);
        if (!active) return;

        const [jobsResult, statsResult, detailResult] = results;
        if (jobsResult.status === "fulfilled") {
          setJobs(jobsResult.value);
          setJobsLoaded(true);
          setJobsError("");
        } else if (jobsResult.reason.name !== "AbortError") {
          setJobsError(jobsResult.reason.message);
          setJobsLoaded(true);
        }

        if (statsResult.status === "fulfilled") {
          setStats(statsResult.value);
          setStatsError("");
        } else if (statsResult.reason.name !== "AbortError") {
          setStatsError(statsResult.reason.message);
        }

        if (selectedId && selectedJobIdRef.current === selectedId) {
          if (detailResult.status === "fulfilled") {
            setSelectedJob(detailResult.value);
            setDetailError("");
            setDetailLoading(false);
          } else if (detailResult.reason.name !== "AbortError") {
            setSelectedJob(null);
            setDetailError(detailResult.reason.message);
            setDetailLoading(false);
          }
        }

        if (
          jobsResult.status === "fulfilled" ||
          statsResult.status === "fulfilled"
        ) {
          setLastUpdated(new Date());
        }
      } finally {
        inFlight = false;
        if (active) {
          timer = window.setTimeout(refresh, 2000);
        }
      }
    }

    refreshRef.current = refresh;
    refresh();

    return () => {
      active = false;
      window.clearTimeout(timer);
      controller?.abort();
      refreshRef.current = null;
    };
  }, []);

  async function handleSubmit(event) {
    event.preventDefault();
    const trimmedPayload = payload.trim();
    if (!trimmedPayload || submitting) return;

    setSubmitting(true);
    setSubmissionMessage(null);
    try {
      const result = await createJob({ payload: trimmedPayload, priority });
      setSubmissionMessage({
        type: "success",
        text: `Job #${result.id} was added to the queue.`,
      });
      setPayload("");
      selectJobId(result.id);
      refreshRef.current?.();
    } catch (error) {
      setSubmissionMessage({ type: "error", text: error.message });
    } finally {
      setSubmitting(false);
    }
  }

  const visibleJobs = jobs.filter((job) => {
    const matchesStatus =
      filterStatus === "ALL" || job.status === filterStatus;
    const query = searchQuery.trim().toLowerCase();
    const matchesQuery =
      !query ||
      String(job.id).includes(query) ||
      job.payload.toLowerCase().includes(query);
    return matchesStatus && matchesQuery;
  });
  const apiAvailable = !jobsError && !statsError;

  return (
    <div className="min-h-screen bg-[#f5f7fb] lg:flex">
      <aside className="relative z-10 flex w-full flex-col bg-[#101827] text-white lg:fixed lg:inset-y-0 lg:w-[248px]">
        <div className="flex items-center justify-between px-6 py-5 lg:justify-start lg:py-7">
          <a aria-label="TaskFlow home" className="flex items-center gap-3" href="#overview">
            <span className="grid h-10 w-10 place-items-center rounded-xl bg-indigo-500 shadow-lg shadow-indigo-950/30">
              <Layers3 aria-hidden="true" className="h-5 w-5" />
            </span>
            <span>
              <span className="font-display block text-lg font-bold tracking-tight">
                TaskFlow
              </span>
              <span className="block text-[10px] font-semibold uppercase tracking-[0.17em] text-slate-400">
                Operations
              </span>
            </span>
          </a>
          <span className="rounded-md border border-slate-700 px-2 py-1 text-[10px] font-semibold tracking-wide text-slate-400 lg:hidden">
            LIVE
          </span>
        </div>

        <div className="hidden px-4 pt-7 lg:block">
          <p className="px-3 text-[10px] font-bold uppercase tracking-[0.18em] text-slate-500">
            Workspace
          </p>
          <nav aria-label="Main navigation" className="mt-3 space-y-1">
            <a
              className="flex items-center gap-3 rounded-xl bg-indigo-500/15 px-3 py-2.5 text-sm font-semibold text-indigo-200"
              href="#overview"
            >
              <Activity aria-hidden="true" className="h-4 w-4" />
              Overview
              <span className="ml-auto h-1.5 w-1.5 rounded-full bg-indigo-300" />
            </a>
            <a
              className="flex items-center gap-3 rounded-xl px-3 py-2.5 text-sm font-medium text-slate-400 transition hover:bg-slate-800 hover:text-white"
              href="#jobs"
            >
              <Command aria-hidden="true" className="h-4 w-4" />
              Job queue
            </a>
            <a
              className="flex items-center gap-3 rounded-xl px-3 py-2.5 text-sm font-medium text-slate-400 transition hover:bg-slate-800 hover:text-white"
              href="#submit"
            >
              <Plus aria-hidden="true" className="h-4 w-4" />
              Create a job
            </a>
          </nav>
        </div>

        <div className="hidden px-4 pt-9 lg:block">
          <p className="px-3 text-[10px] font-bold uppercase tracking-[0.18em] text-slate-500">
            System
          </p>
          <div className="mt-3 rounded-xl border border-slate-800 bg-slate-900/60 p-3">
            <div className="flex items-center gap-2.5">
              <span
                className={`h-2 w-2 rounded-full ${apiAvailable ? "animate-pulse bg-emerald-400" : "bg-rose-400"}`}
              />
              <span className="text-xs font-semibold text-slate-200">
                {apiAvailable ? "API connection" : "Connection issue"}
              </span>
            </div>
            <p className="mt-2 pl-[18px] text-[11px] text-slate-500">
              {apiAvailable ? "Monitoring every 2 seconds" : "Check the API and database"}
            </p>
          </div>
        </div>

        <div className="mt-auto hidden px-5 pb-5 lg:block">
          <div className="flex items-center gap-2 border-t border-slate-800 pt-4 text-[11px] text-slate-500">
            <Code2 aria-hidden="true" className="h-3.5 w-3.5" />
            Task processing engine
            <span className="ml-auto rounded bg-slate-800 px-1.5 py-0.5 font-mono text-[10px] text-slate-400">
              C++17
            </span>
          </div>
        </div>
      </aside>

      <main className="min-w-0 flex-1 px-4 pb-12 pt-5 sm:px-6 lg:ml-[248px] lg:px-10 lg:pt-8 xl:px-12">
        <header
          className="mx-auto flex max-w-[1440px] flex-wrap items-center justify-between gap-4"
          id="overview"
        >
          <div>
            <div className="flex items-center gap-2 text-xs font-medium text-slate-500">
              <span>Workspace</span>
              <ChevronRight aria-hidden="true" className="h-3 w-3" />
              <span className="text-slate-700">Overview</span>
            </div>
            <h1 className="font-display mt-2 text-2xl font-bold tracking-tight text-slate-900 sm:text-[28px]">
              Operations overview
            </h1>
            <p className="mt-1 text-sm text-slate-500">
              Monitor and manage your background jobs.
            </p>
          </div>
          <div className="flex items-center gap-3 self-start pt-1">
            <div className="hidden items-center gap-2 text-xs text-slate-500 sm:flex">
              <span
                className={`h-2 w-2 rounded-full ${apiAvailable ? "bg-emerald-500" : "bg-rose-500"}`}
              />
              {apiAvailable ? "Live monitoring" : "API unavailable"}
            </div>
            <button
              aria-label="Refresh dashboard data"
              className="inline-flex h-10 items-center gap-2 rounded-xl border border-slate-200 bg-white px-3 text-sm font-semibold text-slate-700 shadow-sm transition hover:border-slate-300 hover:bg-slate-50"
              onClick={() => refreshRef.current?.()}
              type="button"
            >
              <RefreshCw aria-hidden="true" className="h-4 w-4" />
              <span className="hidden sm:inline">Refresh</span>
            </button>
          </div>
        </header>

        <div className="fade-in mx-auto mt-7 max-w-[1440px] space-y-8">
          {(jobsError || statsError) && (
            <ApiError
              message={
                [jobsError, statsError].filter(Boolean).join(" ") +
                " The dashboard will keep retrying automatically."
              }
              onRetry={() => refreshRef.current?.()}
            />
          )}

          <section aria-label="Job statistics">
            <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 xl:grid-cols-5">
              <StatCard
                accent="bg-indigo-50 text-indigo-600"
                footnote="All time"
                icon={Layers3}
                label="Total jobs"
                value={stats.total}
              />
              <StatCard
                accent="bg-emerald-50 text-emerald-600"
                footnote="Successfully finished"
                icon={Check}
                label="Completed"
                value={stats.completed}
              />
              <StatCard
                accent="bg-blue-50 text-blue-600"
                footnote="Running now"
                icon={LoaderCircle}
                label="Processing"
                value={stats.processing}
              />
              <StatCard
                accent="bg-amber-50 text-amber-600"
                footnote="Waiting in queue"
                icon={Clock3}
                label="Pending"
                value={stats.pending}
              />
              <StatCard
                accent="bg-rose-50 text-rose-600"
                footnote="Requires attention"
                icon={AlertCircle}
                label="Failed"
                value={stats.failed}
              />
            </div>
            <div className="mt-3 flex items-center justify-end gap-1.5 text-[11px] text-slate-400">
              <Activity aria-hidden="true" className="h-3 w-3" />
              {lastUpdated
                ? `Updated ${lastUpdated.toLocaleTimeString()}`
                : "Waiting for API data"}
            </div>
          </section>

          <section
            aria-labelledby="submit-heading"
            className="relative overflow-hidden rounded-2xl border border-slate-200/80 bg-white shadow-panel"
            id="submit"
          >
            <div className="pointer-events-none absolute inset-y-0 right-0 hidden w-1/3 soft-grid opacity-60 md:block" />
            <div className="relative grid gap-6 p-5 sm:p-6 lg:grid-cols-[minmax(0,1fr)_minmax(300px,0.9fr)] lg:items-center lg:p-7">
              <div>
                <div className="flex items-center gap-2 text-indigo-600">
                  <span className="grid h-8 w-8 place-items-center rounded-lg bg-indigo-50">
                    <Zap aria-hidden="true" className="h-4 w-4" />
                  </span>
                  <p className="text-[11px] font-bold uppercase tracking-[0.18em]">
                    Get things moving
                  </p>
                </div>
                <h2
                  className="font-display mt-3 text-xl font-bold tracking-tight text-slate-900"
                  id="submit-heading"
                >
                  Submit a new job
                </h2>
                <p className="mt-1 max-w-md text-sm leading-6 text-slate-500">
                  Add work to the queue. Higher-priority jobs are scheduled first.
                </p>
              </div>
              <form className="space-y-3" onSubmit={handleSubmit}>
                <label className="sr-only" htmlFor="job-payload">
                  Job payload
                </label>
                <textarea
                  className="min-h-[76px] w-full resize-y rounded-xl border border-slate-200 bg-slate-50/70 px-3.5 py-3 text-sm text-slate-800 placeholder:text-slate-400 focus:border-indigo-400 focus:bg-white"
                  id="job-payload"
                  maxLength={10000}
                  onChange={(event) => setPayload(event.target.value)}
                  placeholder="Describe the work this job should perform..."
                  required
                  value={payload}
                />
                <div className="flex flex-col gap-3 sm:flex-row">
                  <label className="sr-only" htmlFor="job-priority">
                    Job priority
                  </label>
                  <select
                    className="h-11 min-w-36 rounded-xl border border-slate-200 bg-white px-3 text-sm font-medium text-slate-700 focus:border-indigo-400"
                    id="job-priority"
                    onChange={(event) => setPriority(event.target.value)}
                    value={priority}
                  >
                    <option value="HIGH">High priority</option>
                    <option value="MEDIUM">Medium priority</option>
                    <option value="LOW">Low priority</option>
                  </select>
                  <button
                    className="inline-flex h-11 flex-1 items-center justify-center gap-2 rounded-xl bg-indigo-600 px-4 text-sm font-semibold text-white shadow-sm shadow-indigo-200 transition hover:bg-indigo-700 disabled:cursor-not-allowed disabled:opacity-60"
                    disabled={submitting || !payload.trim()}
                    type="submit"
                  >
                    {submitting ? (
                      <LoaderCircle aria-hidden="true" className="h-4 w-4 animate-spin" />
                    ) : (
                      <Plus aria-hidden="true" className="h-4 w-4" />
                    )}
                    {submitting ? "Submitting..." : "Add to queue"}
                  </button>
                </div>
                {submissionMessage && (
                  <p
                    className={`flex items-center gap-2 text-xs font-medium ${submissionMessage.type === "success" ? "text-emerald-700" : "text-rose-700"}`}
                    role={submissionMessage.type === "error" ? "alert" : "status"}
                  >
                    {submissionMessage.type === "success" ? (
                      <CheckCircle2 aria-hidden="true" className="h-4 w-4" />
                    ) : (
                      <AlertCircle aria-hidden="true" className="h-4 w-4" />
                    )}
                    {submissionMessage.text}
                    {submissionMessage.type === "error" && (
                      <button
                        aria-label="Dismiss error"
                        className="ml-auto rounded p-1 hover:bg-rose-50"
                        onClick={() => setSubmissionMessage(null)}
                        type="button"
                      >
                        <X aria-hidden="true" className="h-3.5 w-3.5" />
                      </button>
                    )}
                  </p>
                )}
              </form>
            </div>
          </section>

          <section aria-labelledby="jobs-heading" id="jobs">
            <SectionHeading
              description="A live view of every job tracked by the TaskFlow engine."
              eyebrow="Queue monitor"
              title="Recent jobs"
              action={
                <span className="inline-flex items-center gap-1.5 rounded-full border border-slate-200 bg-white px-3 py-1.5 text-xs font-semibold text-slate-600">
                  <span className="h-1.5 w-1.5 rounded-full bg-emerald-500" />
                  Auto-refresh · 2s
                </span>
              }
            />

            <div className="mt-5 overflow-hidden rounded-2xl border border-slate-200/80 bg-white shadow-panel">
              <div className="flex flex-col gap-3 border-b border-slate-100 px-4 py-4 sm:flex-row sm:items-center sm:justify-between sm:px-5">
                <div className="relative w-full sm:max-w-xs">
                  <Search
                    aria-hidden="true"
                    className="absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-slate-400"
                  />
                  <label className="sr-only" htmlFor="job-search">
                    Search jobs by ID or payload
                  </label>
                  <input
                    className="h-10 w-full rounded-xl border border-slate-200 bg-slate-50/70 pl-9 pr-3 text-sm placeholder:text-slate-400 focus:border-indigo-400 focus:bg-white"
                    id="job-search"
                    onChange={(event) => setSearchQuery(event.target.value)}
                    placeholder="Search jobs..."
                    type="search"
                    value={searchQuery}
                  />
                </div>
                <div className="flex items-center gap-2">
                  <Filter aria-hidden="true" className="h-4 w-4 text-slate-400" />
                  <label className="sr-only" htmlFor="status-filter">
                    Filter jobs by status
                  </label>
                  <select
                    className="h-10 min-w-36 rounded-xl border border-slate-200 bg-white px-3 text-xs font-semibold text-slate-600 focus:border-indigo-400"
                    id="status-filter"
                    onChange={(event) => setFilterStatus(event.target.value)}
                    value={filterStatus}
                  >
                    {STATUSES.map((status) => (
                      <option key={status} value={status}>
                        {status === "ALL" ? "All statuses" : status}
                      </option>
                    ))}
                  </select>
                  <span className="ml-1 hidden text-xs text-slate-400 sm:inline">
                    {jobs.length} {jobs.length === 1 ? "job" : "jobs"}
                  </span>
                </div>
              </div>

              {jobsError && (
                <div className="p-5">
                  <ApiError
                    message={jobsError}
                    onRetry={() => refreshRef.current?.()}
                  />
                </div>
              )}

              {!jobsLoaded && !jobsError ? (
                <div className="space-y-3 p-5" aria-label="Loading jobs">
                  {[0, 1, 2].map((item) => (
                    <div
                      className="h-12 animate-pulse rounded-lg bg-slate-100"
                      key={item}
                    />
                  ))}
                </div>
              ) : jobsLoaded && jobs.length === 0 && !jobsError ? (
                <div className="flex flex-col items-center px-5 py-14 text-center">
                  <span className="grid h-12 w-12 place-items-center rounded-2xl bg-slate-100 text-slate-400">
                    <Layers3 aria-hidden="true" className="h-5 w-5" />
                  </span>
                  <h3 className="mt-4 text-sm font-semibold text-slate-800">
                    No jobs yet
                  </h3>
                  <p className="mt-1 text-sm text-slate-500">
                    Submit your first job to see it appear here.
                  </p>
                  <a
                    className="mt-4 text-sm font-semibold text-indigo-600 hover:text-indigo-700"
                    href="#submit"
                  >
                    Create a job <ArrowUpRight aria-hidden="true" className="inline h-4 w-4" />
                  </a>
                </div>
              ) : jobsLoaded && visibleJobs.length === 0 && !jobsError ? (
                <div className="px-5 py-12 text-center">
                  <p className="text-sm font-semibold text-slate-700">No matching jobs</p>
                  <p className="mt-1 text-sm text-slate-500">
                    Try another search term or status filter.
                  </p>
                </div>
              ) : (
                <div className="overflow-x-auto">
                  <table className="w-full min-w-[740px] text-left">
                    <thead className="bg-slate-50/80 text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                      <tr>
                        <th className="px-5 py-3.5" scope="col">Job ID</th>
                        <th className="px-5 py-3.5" scope="col">Payload</th>
                        <th className="px-5 py-3.5" scope="col">Priority</th>
                        <th className="px-5 py-3.5" scope="col">Status</th>
                        <th className="px-5 py-3.5" scope="col">Attempts</th>
                        <th className="px-4 py-3.5" scope="col">
                          <span className="sr-only">View details</span>
                        </th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-slate-100">
                      {visibleJobs.map((job) => (
                        <tr
                          className={`cursor-pointer transition hover:bg-indigo-50/40 ${selectedJobId === job.id ? "bg-indigo-50/50" : ""}`}
                          key={job.id}
                          onClick={() => selectJobId(job.id)}
                        >
                          <td className="whitespace-nowrap px-5 py-4">
                            <button
                              className="font-mono text-xs font-semibold text-indigo-700 hover:text-indigo-900"
                              onClick={() => selectJobId(job.id)}
                              type="button"
                            >
                              #{job.id}
                            </button>
                          </td>
                          <td className="max-w-[360px] px-5 py-4">
                            <p className="truncate text-sm font-medium text-slate-700" title={job.payload}>
                              {job.payload}
                            </p>
                          </td>
                          <td className="whitespace-nowrap px-5 py-4">
                            <PriorityLabel priority={job.priority} />
                          </td>
                          <td className="whitespace-nowrap px-5 py-4">
                            <StatusBadge status={job.status} />
                          </td>
                          <td className="px-5 py-4 text-sm font-semibold tabular-nums text-slate-600">
                            {job.attempts}
                          </td>
                          <td className="px-4 py-4 text-right">
                            <button
                              aria-label={`View job ${job.id} details`}
                              className="rounded-lg p-1.5 text-slate-400 transition hover:bg-white hover:text-indigo-600"
                              onClick={(event) => {
                                event.stopPropagation();
                                selectJobId(job.id);
                              }}
                              type="button"
                            >
                              <ChevronRight aria-hidden="true" className="h-4 w-4" />
                            </button>
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}

              <div className="flex flex-wrap items-center justify-between gap-2 border-t border-slate-100 px-5 py-3 text-[11px] text-slate-400">
                <span>Showing {visibleJobs.length} of {jobs.length} jobs</span>
                <span className="inline-flex items-center gap-1.5">
                  <Database aria-hidden="true" className="h-3.5 w-3.5" />
                  Persisted job data
                </span>
              </div>
            </div>
          </section>

          {selectedJobId !== null && (
            <section aria-labelledby="detail-heading">
              <SectionHeading
                description={`Current details for job #${selectedJobId}.`}
                eyebrow="Job inspection"
                title="Job details"
                action={
                  <button
                    className="inline-flex items-center gap-1.5 rounded-lg px-2.5 py-2 text-xs font-semibold text-slate-500 transition hover:bg-slate-100 hover:text-slate-800"
                    onClick={() => selectJobId(null)}
                    type="button"
                  >
                    <X aria-hidden="true" className="h-3.5 w-3.5" />
                    Close
                  </button>
                }
              />
              <div className="mt-4 rounded-2xl border border-slate-200/80 bg-white p-5 shadow-panel sm:p-6">
                {detailLoading ? (
                  <div className="flex items-center gap-2 py-4 text-sm text-slate-500">
                    <LoaderCircle aria-hidden="true" className="h-4 w-4 animate-spin" />
                    Loading latest job details...
                  </div>
                ) : detailError ? (
                  <ApiError
                    message={detailError}
                    onRetry={() => refreshRef.current?.()}
                  />
                ) : selectedJob ? (
                  <div className="grid gap-5 sm:grid-cols-2 lg:grid-cols-4">
                    <div>
                      <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                        Job ID
                      </p>
                      <p className="mt-2 font-mono text-sm font-semibold text-slate-800">
                        #{selectedJob.id}
                      </p>
                    </div>
                    <div>
                      <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                        Status
                      </p>
                      <div className="mt-2"><StatusBadge status={selectedJob.status} /></div>
                    </div>
                    <div>
                      <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                        Priority
                      </p>
                      <div className="mt-2"><PriorityLabel priority={selectedJob.priority} /></div>
                    </div>
                    <div>
                      <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                        Attempts
                      </p>
                      <p className="mt-2 text-sm font-semibold tabular-nums text-slate-800">
                        {selectedJob.attempts}
                      </p>
                    </div>
                    <div className="sm:col-span-2 lg:col-span-4">
                      <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                        Payload
                      </p>
                      <p className="mt-2 whitespace-pre-wrap break-words rounded-xl bg-slate-50 px-4 py-3 text-sm leading-6 text-slate-700">
                        {selectedJob.payload}
                      </p>
                    </div>
                    {selectedJob.createdAt && (
                      <div>
                        <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                          Created
                        </p>
                        <p className="mt-2 text-sm text-slate-600">
                          {formatDate(selectedJob.createdAt)}
                        </p>
                      </div>
                    )}
                    {selectedJob.updatedAt && (
                      <div>
                        <p className="text-[10px] font-bold uppercase tracking-[0.14em] text-slate-400">
                          Last updated
                        </p>
                        <p className="mt-2 text-sm text-slate-600">
                          {formatDate(selectedJob.updatedAt)}
                        </p>
                      </div>
                    )}
                  </div>
                ) : (
                  <p className="py-3 text-sm text-slate-500">Job details are not available.</p>
                )}
              </div>
            </section>
          )}

          <footer className="flex flex-col justify-between gap-2 border-t border-slate-200/80 pt-5 text-[11px] text-slate-400 sm:flex-row sm:items-center">
            <span>TaskFlow · Real-time job orchestration</span>
            <span className="inline-flex items-center gap-1.5">
              <Server aria-hidden="true" className="h-3.5 w-3.5" />
              API: {getApiBaseUrl()}
            </span>
          </footer>
        </div>
      </main>
    </div>
  );
}

export default App;
