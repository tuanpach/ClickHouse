---
description: 'Documentation for the sampling query profiler tool in ClickHouse'
sidebar_label: 'Query Profiling'
sidebar_position: 54
slug: /operations/optimizing-performance/sampling-query-profiler
title: 'Sampling query profiler'
---

import SelfManaged from '@site/docs/_snippets/_self_managed_only_no_roadmap.md';

# Sampling query profiler

ClickHouse runs sampling profiler that allows analyzing query execution. Using profiler you can find source code routines that used the most frequently during query execution. You can trace CPU time and wall-clock time spent including idle time.

Query profiler is automatically enabled in ClickHouse Cloud and you can run a sample query as follows

:::note If you are running the following query in ClickHouse Cloud, make sure to change `FROM system.trace_log` to `FROM clusterAllReplicas(default, system.trace_log)` to select from all nodes of the cluster
:::

```sql
SELECT
    count(),
    arrayStringConcat(arrayMap(x -> concat(demangle(addressToSymbol(x)), '\n    ', addressToLine(x)), trace), '\n') AS sym
FROM system.trace_log
WHERE query_id = 'ebca3574-ad0a-400a-9cbc-dca382f5998c' AND trace_type = 'CPU' AND event_date = today()
GROUP BY trace
ORDER BY count() DESC
LIMIT 10
SETTINGS allow_introspection_functions = 1
```

In self-managed deployments, to use query profiler:

- Setup the [trace_log](../../operations/server-configuration-parameters/settings.md#trace_log) section of the server configuration.

    This section configures the [trace_log](/operations/system-tables/trace_log) system table containing the results of the profiler functioning. It is configured by default. Remember that data in this table is valid only for a running server. After the server restart, ClickHouse does not clean up the table and all the stored virtual memory address may become invalid.

- Setup the [query_profiler_cpu_time_period_ns](../../operations/settings/settings.md#query_profiler_cpu_time_period_ns) or [query_profiler_real_time_period_ns](../../operations/settings/settings.md#query_profiler_real_time_period_ns) settings. Both settings can be used simultaneously.

    These settings allow you to configure profiler timers. As these are the session settings, you can get different sampling frequency for the whole server, individual users or user profiles, for your interactive session, and for each individual query.

The default sampling frequency is one sample per second and both CPU and real timers are enabled. This frequency allows collecting enough information about ClickHouse cluster. At the same time, working with this frequency, profiler does not affect ClickHouse server's performance. If you need to profile each individual query try to use higher sampling frequency.

To analyze the `trace_log` system table:

- Install the `clickhouse-common-static-dbg` package. See [Install from DEB Packages](../../getting-started/install/install.mdx).

- Allow introspection functions by the [allow_introspection_functions](../../operations/settings/settings.md#allow_introspection_functions) setting.

    For security reasons, introspection functions are disabled by default.

- Use the `addressToLine`, `addressToLineWithInlines`, `addressToSymbol` and `demangle` [introspection functions](../../sql-reference/functions/introspection.md) to get function names and their positions in ClickHouse code. To get a profile for some query, you need to aggregate data from the `trace_log` table. You can aggregate data by individual functions or by the whole stack traces.

If you need to visualize `trace_log` info, try [flamegraph](/interfaces/third-party/gui#clickhouse-flamegraph) and [speedscope](https://github.com/laplab/clickhouse-speedscope).

## Example {#example}

In this example we:

- Filtering `trace_log` data by a query identifier and the current date.

- Aggregating by stack trace.

- Using introspection functions, we will get a report of:

  - Names of symbols and corresponding source code functions.
  - Source code locations of these functions.

<!-- -->

```sql
SELECT
    count(),
    arrayStringConcat(arrayMap(x -> concat(demangle(addressToSymbol(x)), '\n    ', addressToLine(x)), trace), '\n') AS sym
FROM system.trace_log
WHERE (query_id = 'ebca3574-ad0a-400a-9cbc-dca382f5998c') AND (event_date = today())
GROUP BY trace
ORDER BY count() DESC
LIMIT 10
```
