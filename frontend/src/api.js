const API_BASE_URL = (import.meta.env.VITE_API_BASE_URL || "/api").replace(
  /\/+$/,
  "",
);

async function request(path, options) {
  let response;
  try {
    response = await fetch(`${API_BASE_URL}${path}`, options);
  } catch (error) {
    if (error.name === "AbortError") {
      throw error;
    }
    throw new Error(
      `Cannot reach the TaskFlow API at ${API_BASE_URL}. Check that the C++ service is running and MongoDB is available.`,
    );
  }

  let body;
  try {
    body = await response.json();
  } catch {
    throw new Error(
      `The TaskFlow API returned an unreadable response (HTTP ${response.status}).`,
    );
  }

  if (!response.ok) {
    const message =
      typeof body?.error === "string"
        ? body.error
        : `The TaskFlow API request failed (HTTP ${response.status}).`;
    throw new Error(
      response.status === 503
        ? `The API or MongoDB is unavailable: ${message}`
        : message,
    );
  }

  return body;
}

export function getJobs(signal) {
  return request("/jobs", { signal });
}

export function getStats(signal) {
  return request("/stats", { signal });
}

export function getJob(id, signal) {
  return request(`/jobs/${encodeURIComponent(id)}`, { signal });
}

export function createJob(job) {
  return request("/jobs", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(job),
  });
}

export function getApiBaseUrl() {
  return API_BASE_URL;
}
