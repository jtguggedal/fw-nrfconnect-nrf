#!/usr/bin/env python3

import argparse
import getpass
import netrc
import os
import sys

import pygit2
from github import Github

def parse_args():
    parser = argparse.ArgumentParser(
        epilog='''Get a list of pull requests associated
        with a range of commits in a GitHub repository.

        For prompt-less operation, either set up a ~/.netrc with
        credentials for github.com, or define the GITHUB_USERNAME and
        GITHUB_TOKEN environment variables.''')
    parser.add_argument('gh_repo',
                        help='''repository in <organization>/<repo> format,
                        like nrfconnect/sdk-nrf''')
    parser.add_argument('local_path',
                        help='path to repository on the file system')
    parser.add_argument('start', help='SHA of start commit in the range')
    parser.add_argument('end', help='SHA of end commit in the range')
    return parser.parse_args()

def get_gh_credentials():
    user = None
    token = None

    try:
        nrc = netrc.netrc()
    except FileNotFoundError:
        nrc = None
    except netrc.NetrcParseError:
        print('parse error in netrc file, falling back on environment',
              file=sys.stderr)
        nrc = None

    if nrc is not None:
        auth = nrc.authenticators('github.com')
        if auth is not None:
            login, _, password = auth
            user = login
            token = password

    if user is None:
        user = os.environ.get('GITHUB_USERNAME')
    if token is None:
        token = os.environ.get('GITHUB_TOKEN')

    if not (user and token):
        print('Missing GitHub credentials:\n'
              '~/.netrc file not found or missing github.com credentials, '
              'and at least one of GITHUB_USERNAME/GITHUB_TOKEN is not set '
              'in the environment. Please specify your GitHub user/pass.',
              file=sys.stderr)
        if not user:
            print('username: ', file=sys.stderr, end='', flush=True)
            user = input()
        if not token:
            token = getpass.getpass('password/token: ')

    return user, token

def get_gh_repo(args):
    user, token = get_gh_credentials()
    gh = Github(user, token)
    return gh.get_repo(args.gh_repo)

def get_pygit2_commits(args):
    repo = pygit2.Repository(args.local_path)
    walker = repo.walk(args.end)
    walker.hide(args.start)
    return list(walker)

# get list of commits from args.local_path
args = parse_args()
commit_list = get_pygit2_commits(args)

# get associated PRs using GitHub API
sha_to_pr = {}
gh_repo = get_gh_repo(args)
for commit in commit_list:
    sha = str(commit.oid)

    gh_prs = list(gh_repo.get_commit(sha).get_pulls())
    if len(gh_prs) != 1:
        sys.exit(f'{sha} has {len(gh_prs)} prs (expected 1): {gh_prs}')
    gh_pr = gh_prs[0]
    sha_to_pr[sha] = gh_pr
    print(f'{sha} was merged in PR #{gh_pr.number}: {gh_pr.html_url}',
          file=sys.stderr)
print(file=sys.stderr)

# post-processing
prs = {pr.html_url: pr for pr in sha_to_pr.values()}
sorted_urls = sorted(prs.keys())
print(f'{len(prs)} pull requests merged:')
for url in sorted_urls:
    pr = prs[url]
    print(f'- #{pr.number}: {pr.title} {url}')
