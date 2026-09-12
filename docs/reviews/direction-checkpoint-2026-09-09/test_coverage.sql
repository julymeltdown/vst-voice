SELECT outcome AS status,
       COUNT(*) AS entries,
       (SELECT COUNT(*) FROM registered_test_results) AS registered,
       '2026-09-09' AS reviewDate
FROM registered_test_results
GROUP BY outcome
ORDER BY entries DESC, outcome;
