SELECT d.id, d.label, d.case_name AS caseName, d.location, d.suites,
       COUNT(*) AS appearances
FROM main.failed_case_events AS e
JOIN main.failure_case_definitions AS d ON d.case_name = e.case_name
GROUP BY d.id, d.label, d.case_name, d.location, d.suites
ORDER BY appearances DESC, d.id ASC;
