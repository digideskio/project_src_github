<?php

/**
 * @file
 * Project Source: GitHub callback handler for tarball repackaging.
 */


/**
 * Menu callback that returns the archive from GitHub in a way that drush and
 * other associated package handling systems can digest.
 *
 * @param $namespace
 *   The namespace (or group) under which the project exists.
 *
 * @param $project_name
 *   The name of the project.
 *
 * @param $version
 *   The version of the project.
 *
 * @return
 *   The archive as a string, suitable for streaming.
 */
function project_src_github_get_archive($namespace, $project_name, $version) {
  $github = 'https://github.com';
  $extension = $namespace . '/' . $project_name;
  $get = $github . '/' . $extension . '/archive/' . $version . '.tar.gz';

  // Attempt to load the tarball from GitHub as a string.
  if ($file = _project_src_github_file_as_string($get)) {
    // The following can potentially be time-consuming and there may be issues
    // if multiple requests attempt to be fulfilled for the same project.
    $lock_name = __FUNCTION__ . '-' . $project_name;
    if (lock_acquire($lock_name)) {
      // If we've acquired the lock, process as usual.
      $file = _project_src_github_repackage_tarball($file, $get, $project_name, $version);

      // Then release the lock.
      lock_release($lock_name);
    }
    else {
      // Continue attempting to acquire the lock indefinitely.
      while (lock_wait($lock_name)) {}

      // If we're here, the lock may be acquired.
      lock_acquire($lock_name);
      $file = _project_src_github_repackage_tarball($file);
      lock_release($lock_name);
    }

    drupal_add_http_header('Content-type', 'application/octet-stream');
    drupal_add_http_header('Content-transfer-encoding', 'binary');
    drupal_add_http_header('Content-disposition', 'attachment; filename="' . $project_name . '-' . $version . '.tar.gz"');
  }
  else {
    $file = '';
  }

  return $file;
}
